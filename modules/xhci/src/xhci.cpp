#include "xhci/xhci.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "memory/kalloc.h"

void XHCIModule::reset_controller() {
	op_regs->usbcmd &= ~XHCI_CMD_RS;
	while (!(op_regs->usbsts & XHCI_STS_HCH)) {
		IOUtils::sleep(1);
	}

	op_regs->usbcmd |= XHCI_CMD_HCRST;
	while (op_regs->usbcmd & XHCI_CMD_HCRST) {
		IOUtils::sleep(1);
	}

	while (op_regs->usbsts & XHCI_STS_CNR) {
		IOUtils::sleep(1);
	}

	log(mod, "Controller reset complete");
}

void XHCIModule::init_controller() {
	num_slots = cap_regs->hcsparams1 & 0xFF;
	num_ports = (cap_regs->hcsparams1 >> 24) & 0xFF;
	max_intrs = (cap_regs->hcsparams1 >> 8) & 0x7FF;

	log(mod, "Slots: %d, Ports: %d, Interrupters: %d", num_slots, num_ports,
	    max_intrs);

	op_regs->config = num_slots;

	uintptr_t dcbaa_paddr = 0;
	dcbaa = (uint64_t*) IOUtils::DMAAlloc(
		sizeof(uint64_t) * (num_slots + 1), &dcbaa_paddr);
	IOUtils::memset(dcbaa, 0, sizeof(uint64_t) * (num_slots + 1));
	op_regs->dcbaap = (uint64_t) dcbaa_paddr;
	dcbaa_phys = dcbaa_paddr;

	uintptr_t cmd_ring_paddr = 0;
	cmd_ring = (struct xhci_trb*) IOUtils::DMAAlloc(
		sizeof(struct xhci_trb) * 64, &cmd_ring_paddr);
	IOUtils::memset(cmd_ring, 0, sizeof(struct xhci_trb) * 64);
	op_regs->crcr = (uint64_t) cmd_ring_paddr | 1;
	cmd_ring_phys = cmd_ring_paddr;
	cmd_ring_index = 0;
	cmd_ring_pcs = 1;

	uintptr_t erst_paddr = 0;
	erst = (struct xhci_erst_entry*) IOUtils::DMAAlloc(
		sizeof(struct xhci_erst_entry), &erst_paddr);

	uintptr_t event_ring_paddr = 0;
	event_ring = (struct xhci_trb*) IOUtils::DMAAlloc(
		sizeof(struct xhci_trb) * 64, &event_ring_paddr);
	IOUtils::memset(event_ring, 0, sizeof(struct xhci_trb) * 64);

	erst[0].ba = (uint64_t) event_ring_paddr;
	erst[0].size = 64;
	erst[0].reserved = 0;

	runtime_regs->ir[0].erstsz = 1;
	runtime_regs->ir[0].erdp =
		(uint64_t) event_ring_paddr | (1 << 3); // EHB
	runtime_regs->ir[0].erstba = (uint64_t) erst_paddr;
	runtime_regs->ir[0].iman |= 3;

	event_ring_phys = event_ring_paddr;
	erst_phys = erst_paddr;
	event_ring_index = 0;
	event_ring_pcs = 1;

	op_regs->usbcmd |= XHCI_CMD_RS;
	while (op_regs->usbsts & XHCI_STS_HCH) {
		IOUtils::sleep(1);
	}

	log(mod, "Controller started");
}

void XHCIModule::probe_ports() {
	volatile uint32_t* portsc =
		(volatile uint32_t*) ((uintptr_t) op_regs + 0x400);

	for (uint32_t i = 0; i < num_ports; i++) {
		volatile uint32_t* reg = &portsc[i * 4];
		uint32_t status = *reg;

		if (status & XHCI_PORT_CCS) {
			uint32_t speed = (status >> 10) & 0xF;
			log(mod, "Port %d: Connected (Speed %d)", i + 1, speed);

			if (!(status & XHCI_PORT_PED)) {
				log(mod, "Port %d: Resetting...", i + 1);
				*reg |= XHCI_PORT_PR;
				while (*reg & XHCI_PORT_PR) {
					IOUtils::sleep(1);
				}
			}

			// After reset, enable slot and address device
			uint8_t slot_id = enable_slot();
			if (slot_id) {
				log(mod, "Port %d: Slot %d enabled", i + 1,
				    slot_id);
				address_device(slot_id, i + 1);
			}
		}
	}
}

void XHCIModule::send_command(struct xhci_trb* trb) {
	uint32_t index = cmd_ring_index;
	cmd_ring[index].ptr = trb->ptr;
	cmd_ring[index].status = trb->status;

	uint32_t ctrl = trb->control & ~1;
	if (cmd_ring_pcs)
		ctrl |= 1;
	cmd_ring[index].control = ctrl;

	cmd_ring_index++;
	if (cmd_ring_index >= 63) {
		// Handle link TRB if needed, simplified for now
		cmd_ring_index = 0;
	}

	doorbell_regs[0] = 0; // Host controller command ring doorbell
}

struct xhci_trb XHCIModule::wait_for_event(uint8_t type) {
	for (int timeout = 0; timeout < 1000; timeout++) {
		struct xhci_trb event = event_ring[event_ring_index];
		uint8_t pcs = event.control & 1;

		if (pcs == event_ring_pcs) {
			uint8_t ev_type = (event.control >> 10) & 0x3F;

			// Update cycle bit logic
			event_ring_index++;
			if (event_ring_index >= 64) {
				event_ring_index = 0;
				event_ring_pcs ^= 1;
			}

			runtime_regs->ir[0].erdp =
				(event_ring_phys
				 + (event_ring_index * sizeof(struct xhci_trb)))
				| (1 << 3);

			if (ev_type == type)
				return event;
		}
		IOUtils::sleep(1);
	}
	struct xhci_trb empty = {0, 0, 0};
	return empty;
}

uint8_t XHCIModule::enable_slot() {
	struct xhci_trb cmd = {0, 0, (XHCI_TRB_ENABLE_SLOT_CMD << 10)};
	send_command(&cmd);
	struct xhci_trb ev = wait_for_event(XHCI_TRB_COMMAND_COMPLETION_EVENT);
	return (ev.control >> 24) & 0xFF;
}

void XHCIModule::address_device(uint8_t slot_id, uint8_t port_id) {
	uintptr_t ctx_paddr = 0;
	struct xhci_device_ctx* dev_ctx =
		(struct xhci_device_ctx*) IOUtils::DMAAlloc(
			sizeof(struct xhci_device_ctx), &ctx_paddr);
	IOUtils::memset(dev_ctx, 0, sizeof(struct xhci_device_ctx));

	slots[slot_id].ctx = dev_ctx;
	slots[slot_id].ctx_phys = ctx_paddr;
	slots[slot_id].port_id = port_id;
	slots[slot_id].active = true;
	dcbaa[slot_id] = (uint64_t) ctx_paddr;

	// Set Slot Context
	dev_ctx->slot.info = (1 << 27); // Context entries = 1 (only EP0)
	dev_ctx->slot.info2 = port_id;	// Root hub port number

	// Set EP0 Context
	uintptr_t ring_paddr = 0;
	struct xhci_trb* ring = create_transfer_ring(&ring_paddr);
	dev_ctx->ep[0].info2 = (4 << 3) | (3 << 1); // Control, Error Count 3
	dev_ctx->ep[0].info2 |=
		(64 << 16); // Max Packet Size 64 (default for FS/HS/SS)
	dev_ctx->ep[0].trdp = (uint64_t) ring_paddr | 1; // DCS = 1

	slots[slot_id].rings[0] = ring;
	slots[slot_id].rings_phys[0] = ring_paddr;
	slots[slot_id].ring_indices[0] = 0;
	slots[slot_id].ring_pcs[0] = 1;

	uintptr_t input_paddr = 0;
	struct xhci_input_ctx* input_ctx =
		(struct xhci_input_ctx*) IOUtils::DMAAlloc(
			sizeof(struct xhci_input_ctx), &input_paddr);
	IOUtils::memset(input_ctx, 0, sizeof(struct xhci_input_ctx));

	input_ctx->input_control.add_flags = 3; // Slot and EP0
	IOUtils::memcpy(&input_ctx->device, dev_ctx,
			sizeof(struct xhci_device_ctx));

	struct xhci_trb cmd = {(uint64_t) input_paddr, 0,
			       (uint32_t) ((XHCI_TRB_ADDRESS_DEVICE_CMD << 10)
					   | (slot_id << 24))};
	send_command(&cmd);
	wait_for_event(XHCI_TRB_COMMAND_COMPLETION_EVENT);
}

struct xhci_trb* XHCIModule::create_transfer_ring(uintptr_t* phys) {
	struct xhci_trb* ring = (struct xhci_trb*) IOUtils::DMAAlloc(
		sizeof(struct xhci_trb) * 64, phys);
	IOUtils::memset(ring, 0, sizeof(struct xhci_trb) * 64);
	return ring;
}

void XHCIModule::send_async_with_response(uint8_t addr, uint8_t /*endpoint*/,
					  uint32_t data_phys, size_t /*size*/,
					  uint32_t response_phys,
					  size_t response_size) {
	uint8_t slot_id =
		addr; // In XHCI, addr corresponds to slot_id in this simplified driver
	if (!slots[slot_id].active)
		return;

	uint32_t ep_idx = 0; // EP0 for control transfers
	struct xhci_trb* ring = slots[slot_id].rings[ep_idx];
	uint32_t idx = slots[slot_id].ring_indices[ep_idx];
	uint8_t pcs = slots[slot_id].ring_pcs[ep_idx];

	// 1. Setup Stage
	ring[idx].ptr = data_phys; // Packet data (8 bytes setup)
	ring[idx].status = 8;	   // Length
	uint32_t ctrl =
		(XHCI_TRB_SETUP_STAGE << 10)
		| (1
		   << 6); // IDT (Immediate Data) - Wait, no, setup is 8 bytes in buffer
	if (response_phys && response_size > 0)
		ctrl |= (3 << 16); // TRT = 3 (IN)
	else
		ctrl |= (2 << 16); // TRT = 2 (OUT/NoData)
	if (pcs)
		ctrl |= 1;
	ring[idx].control = ctrl;

	idx++;
	if (idx >= 63)
		idx = 0; // Simplified ring wrap

	// 2. Data Stage (Optional)
	if (response_phys && response_size > 0) {
		ring[idx].ptr = response_phys;
		ring[idx].status = response_size;
		ctrl = (XHCI_TRB_DATA_STAGE << 10);
		ctrl |= (1 << 16); // DIR = 1 (IN)
		if (pcs)
			ctrl |= 1;
		ring[idx].control = ctrl;
		idx++;
		if (idx >= 63)
			idx = 0;
	}

	// 3. Status Stage
	ring[idx].ptr = 0;
	ring[idx].status = 0;
	ctrl = (XHCI_TRB_STATUS_STAGE << 10) | (1 << 5); // IOC
	if (pcs)
		ctrl |= 1;
	ring[idx].control = ctrl;
	idx++;
	if (idx >= 63)
		idx = 0;

	slots[slot_id].ring_indices[ep_idx] = idx;
	doorbell_regs[slot_id] = ep_idx + 1; // Doorbell for EP

	wait_for_event(XHCI_TRB_TRANSFER_EVENT);
}
