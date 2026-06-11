#include "xhci/xhci.hpp"
#include "xhci/xhci_pipe.hpp"
#include <cpu/irq_lock.h>
#include <ioforge/ioforge.h>
#include <ioforge/ioforge.hpp>
#include <ioforge/ioforge_new.hpp>
#include <memory/kalloc.h>
#include <spinlock.h>
#include <str.h>
#include <usb.h>

bool XHCIModule::irq_driven = false;

/* Event ring state */
static struct xhci_trb last_sync_ev;
static volatile bool has_sync_ev = false;
static spinlock_t xhci_event_lock;

/* Controller lifecycle */
void XHCIModule::reset_controller() {
	op_regs->usbcmd &= ~XHCI_CMD_RS;
	while (!(op_regs->usbsts & XHCI_STS_HCH))
		IOUtils::sleep(1);

	op_regs->usbcmd |= XHCI_CMD_HCRST;
	while (op_regs->usbcmd & XHCI_CMD_HCRST)
		IOUtils::sleep(1);
	while (op_regs->usbsts & XHCI_STS_CNR)
		IOUtils::sleep(1);

	log(mod, "Controller reset complete");
}

void XHCIModule::init_controller() {
	uint32_t cap_first = *(volatile uint32_t*)cap_regs;
	uint32_t hci_ver = cap_first >> 16;
	uint32_t cap_len = cap_first & 0xFF;

	num_slots = cap_regs->hcsparams1 & 0xFF;
	num_ports = (cap_regs->hcsparams1 >> 24) & 0xFF;
	max_intrs = (cap_regs->hcsparams1 >> 8) & 0x7FF;
	context_size = (cap_regs->hccparams1 & (1 << 2)) ? 64 : 32;

	log(mod, "CAPS: 0x%x (LEN %d, VER 0x%x)", cap_first, cap_len, hci_ver);
	log(mod, "Slots: %d, Ports: %d, Interrupters: %d", num_slots, num_ports,
	    max_intrs);
	log(mod, "HCSPARAMS2: 0x%x", cap_regs->hcsparams2);
	log(mod, "HCCPARAMS1: 0x%x (Context Size: %d)", cap_regs->hccparams1,
	    context_size);

	op_regs->config = num_slots;

	//  DCBAA: 64-byte aligned
	{
		auto a =
		    dma_alloc_aligned(sizeof(uint64_t) * (num_slots + 1), 64);
		memset(a.vaddr, 0, sizeof(uint64_t) * (num_slots + 1));
		dcbaa = (volatile uint64_t*)a.vaddr;
		dcbaa_phys = a.paddr;
		op_regs->dcbaap = (uint64_t)a.paddr;
	}

	//  Scratchpad: page-aligned
	uint32_t hcsparams2 = cap_regs->hcsparams2;
	uint32_t max_scratchpad =
	    ((hcsparams2 >> 27) & 0x1F) << 5 | ((hcsparams2 >> 21) & 0x1F);
	log(mod, "max scratchpad : %d\n", max_scratchpad);

	if (max_scratchpad > 0) {
		log(mod, "Allocating %d scratchpad buffers", max_scratchpad);
		auto sa =
		    dma_alloc_aligned(sizeof(uint64_t) * max_scratchpad, 64);
		auto* sp_array = (uint64_t*)sa.vaddr;
		for (uint32_t i = 0; i < max_scratchpad; i++) {
			auto pg = dma_alloc_aligned(4096, 4096);
			sp_array[i] = (uint64_t)pg.paddr;
		}
		dcbaa[0] = (uint64_t)sa.paddr;
	}

	//  Command ring: 64-byte aligned
	{
		auto a = dma_alloc_aligned(sizeof(struct xhci_trb) * 64, 64);
		IOUtils::memset(a.vaddr, 0, sizeof(struct xhci_trb) * 64);
		cmd_ring = (volatile struct xhci_trb*)a.vaddr;
		cmd_ring_phys = a.paddr;
		cmd_ring_index = 0;
		cmd_ring_pcs = 1;
		op_regs->crcr = (uint64_t)a.paddr | 1;
	}

	auto hcsparams = cap_regs->hcsparams1;
	max_intrs = (hcsparams >> 8) & 0x7FF;
	if (max_intrs > 4)
		max_intrs = 4;
	log(mod, "Using max interrupts: %d\n", max_intrs);

	auto esrt_max = (hcsparams2 >> 4) & 0xF;
	log(mod, "esrt max : %d\n", esrt_max);

	for (uint32_t i = 0; i < max_intrs; i++) {
		auto ea = dma_alloc_aligned(sizeof(struct xhci_trb) * 64, 64);
		IOUtils::memset(ea.vaddr, 0, sizeof(struct xhci_trb) * 64);
		event_ring[i] = (volatile struct xhci_trb*)ea.vaddr;
		event_ring_phys[i] = ea.paddr;
		event_ring_index[i] = 0;
		event_ring_pcs[i] = 1;

		auto esta =
		    dma_alloc_aligned(sizeof(struct xhci_erst_entry), 64);
		erst[i] = (struct xhci_erst_entry*)esta.vaddr;
		erst_phys[i] = esta.paddr;

		erst[i][0].ba = ea.paddr;
		erst[i][0].size = 64;
		erst[i][0].reserved = 0;

		runtime_regs->ir[i].erstsz = 1;
		runtime_regs->ir[i].erdp = ea.paddr | (1u << 3);
		runtime_regs->ir[i].erstba = esta.paddr;
		runtime_regs->ir[i].iman |= 3;
	}

	// Start controller — RS only, INTE added after enumeration
	op_regs->usbcmd |= XHCI_CMD_RS;
	while (op_regs->usbsts & XHCI_STS_HCH)
		IOUtils::sleep(1);

	log(mod, "Controller started (polling mode, IRQ-driven pending)");
}

void XHCIModule::enable_irq_driven_mode() {
	irq_driven = true;

	// Drain any events that accumulated while irq_driven was false
	for (uint32_t i = 0; i < max_intrs; i++) {
		while (true) {
			uint32_t c = event_ring[i][event_ring_index[i]].control;
			if ((c & 1) != event_ring_pcs[i])
				break;
			process_events();
			has_sync_ev = false;
		}
	}

	op_regs->usbcmd |= XHCI_CMD_INTE;

	for (uint32_t i = 0; i < max_intrs; i++) {
		runtime_regs->ir[i].imod = 0;
		runtime_regs->ir[i].iman |= (1u << 1);
	}

	if (op_regs->usbsts & XHCI_STS_EINT)
		op_regs->usbsts = XHCI_STS_EINT;

	for (uint32_t i = 0; i < max_intrs; i++) {
		runtime_regs->ir[i].iman |= (1u << 0);
	}
	log(mod, "IRQ-driven mode enabled");
}

/* Port enumeration */
void XHCIModule::probe_ports() {
	op_regs->usbcmd &= ~XHCI_CMD_INTE;
	runtime_regs->ir[0].iman &= ~(1u << 1); // IE off
	if (op_regs->usbsts & XHCI_STS_EINT)
		op_regs->usbsts = XHCI_STS_EINT;
	irq_driven = false;

	volatile uint32_t* portsc =
	    (volatile uint32_t*)((uintptr_t)op_regs + 0x400);

	for (uint32_t i = 0; i < num_ports; i++) {
		volatile uint32_t* reg = &portsc[i * 4];
		uint32_t status = *reg;

		if (!(status & XHCI_PORT_CCS))
			continue;

		// Jika port sudah di-enable (berarti sudah di-enumerate
		// sebelumnya), skip!
		if (status & XHCI_PORT_PED)
			continue;

		uint32_t speed = (status >> 10) & 0xF;
		log(mod, "Port %d: Connected (Speed %d)", i + 1, speed);

		log(mod, "Port %d: Resetting...", i + 1);

		uint32_t rw = (*reg) & ~XHCI_PORTSC_W1C_MASK;
		*reg = rw | XHCI_PORT_PR;
		while (*reg & XHCI_PORT_PR)
			IOUtils::sleep(1);

		status = *reg;
		speed = (status >> 10) & 0xF;

		*reg = XHCI_PORT_PRC | XHCI_PORT_CSC;

		uint8_t slot_id = enable_slot();
		if (!slot_id)
			continue;

		log(mod, "Port %d: Slot %d enabled (speed %d)", i + 1, slot_id,
		    speed);

		if (!address_device(slot_id, i + 1, speed))
			continue;

		// Device enumeration
		auto* usbDevice = (struct ioforge_usb_device*)kalloc(
		    sizeof(struct ioforge_usb_device));
		IOUtils::memset(usbDevice, 0, sizeof(*usbDevice));

		uint8_t* data = (uint8_t*)kalloc(0x1000);
		IOUtils::memset(data, 0, 0x1000);

		usb_get_descriptor(slot_id, 1, 0, sizeof(usb_device_descriptor),
		                   data);
		auto* dev = (usb_device_descriptor*)data;

		if (dev->idVendor == 0) {
			log(mod,
			    "[XHCI] Port %d: Failed to get descriptor for slot "
			    "%d",
			    i + 1, slot_id);
			IOUtils::free(data, 0x1000);
			IOUtils::free(usbDevice,
			              sizeof(struct ioforge_usb_device));
			continue;
		}

		char iManufacturer[64] = {0};
		char iProduct[64] = {0};
		if (dev->iManufacturer)
			usb_get_string_descriptor(slot_id, dev->iManufacturer,
			                          iManufacturer,
			                          sizeof(iManufacturer));
		if (dev->iProduct)
			usb_get_string_descriptor(slot_id, dev->iProduct,
			                          iProduct, sizeof(iProduct));

		log(mod, "[XHCI] Device on Port %d:", i + 1);
		log(mod, "  VID=0x%x PID=0x%x", dev->idVendor, dev->idProduct);
		log(mod, "  Mfr: %s  Prod: %s",
		    dev->iManufacturer ? iManufacturer : "Unknown",
		    dev->iProduct ? iProduct : "Unknown");

		uint8_t dev_class = dev->bDeviceClass;
		uint8_t dev_subclass = dev->bDeviceSubClass;
		uint8_t dev_protocol = dev->bDeviceProtocol;
		uint8_t ep_count = 0;

		for (int j = 0; j < dev->bNumConfigurations; j++) {
			IOUtils::memset(data, 0, 0x1000);
			usb_get_descriptor(slot_id, 2, j,
			                   sizeof(usb_config_descriptor), data);
			auto conf = (usb_config_descriptor*)data;
			log(mod, "configuration value : %d\n", conf->bConfigurationValue);
			uint16_t total_len =
			    ((usb_config_descriptor*)data)->wTotalLength;
			if (total_len > 0x1000)
				total_len = 0x1000;
			usb_get_descriptor(slot_id, 2, j, total_len, data);

			uint16_t offset =
			    ((usb_config_descriptor*)data)->bLength;
			while (offset < total_len) {
				uint8_t len = data[offset];
				uint8_t type = data[offset + 1];
				if (len == 0)
					break; // prevent infinite loop on
					       // malformed descriptor

				if (type == 4) { // Interface Descriptor
					usb_interface* iface =
					    (usb_interface*)(data + offset);
					char iInterface[64] = {0};
					if (iface->iInterface) {
						usb_get_string_descriptor(
						    slot_id, iface->iInterface,
						    iInterface,
						    sizeof(iInterface));
					}
					log(mod,
					    "[XHCI]   -> Interface %d (Alt "
					    "%d): Class=0x%x Sub=0x%x "
					    "Proto=0x%x Name: %s",
					    iface->bInterfaceNumber,
					    iface->bAlternateSetting,
					    iface->bInterfaceClass,
					    iface->bInterfaceSubClass,
					    iface->bInterfaceProtocol,
					    iface->iInterface ? iInterface
					                      : "No String");
				}
				offset += len;
			}

			uint8_t* ptr = data;
			uint8_t* end = data + total_len;
			while (ptr < end && ptr[0] != 0) {
				if (ptr[1] == 0x04) {
					auto* iface =
					    (struct usb_interface*)ptr;
					if (dev_class == 0) {
						dev_class =
						    iface->bInterfaceClass;
						dev_subclass =
						    iface->bInterfaceSubClass;
						dev_protocol =
						    iface->bInterfaceProtocol;
					}
				} else if (ptr[1] == 0x05 && ep_count < 16) {
					auto* ep =
					    (struct usb_endpoint_descriptor*)
					        ptr;
					usbDevice->endpoints[ep_count].address =
					    ep->bEndpointAddress;
					usbDevice->endpoints[ep_count]
					    .attributes = ep->bmAttributes;
					usbDevice->endpoints[ep_count]
					    .interval = ep->bInterval;
					usbDevice->endpoints[ep_count]
					    .max_packet = ep->wMaxPacketSize;
					ep_count++;
				}
				ptr += ptr[0];
			}
		}

		log(mod, "  Class=%d Sub=%d Proto=%d", dev_class, dev_subclass,
		    dev_protocol);

		usbDevice->vendor_id = dev->idVendor;
		usbDevice->product_id = dev->idProduct;
		usbDevice->class_code = dev_class;
		usbDevice->subclass_code = dev_subclass;
		usbDevice->protocol = dev_protocol;
		usbDevice->usb_version = IoForgeUSB_VERSION_3;
		usbDevice->controller = controller;
		usbDevice->ep_count = ep_count;
		usbDevice->addr = slot_id;
		usbDevice->speed = speed;

		if (dev_class == 0x03) {
			IOUtils::strcopy((char*)usbDevice->base.name,
			                 (char*)"HID Device");
			if (dev_protocol == 1) {
				IOUtils::strcopy((char*)usbDevice->base.name,
				                 (char*)"USB Keyboard");
				log(mod, "  Type: Keyboard");
			} else if (dev_protocol == 2) {
				IOUtils::strcopy((char*)usbDevice->base.name,
				                 (char*)"USB Mouse");
				log(mod, "  Type: Mouse");
			}
		} else {
			IOUtils::strcopy((char*)usbDevice->base.name,
			                 (char*)"USB Device");
		}

		usbDevice->base.type = IOFORGE_USB_DEVICE;

		void* mem = kalloc((sizeof(XHCIPipe) + 31) & ~31u);
		usbDevice->pipe = new (mem) XHCIPipe(this);

		ioforge_attach(ioforge_get_usb_devices_root(),
		               &usbDevice->base);
		IOUtils::free(data, 0x1000);
	}

	enable_irq_driven_mode();
}

/* Command ring */
void XHCIModule::send_command(uint64_t ptr, uint32_t status, uint32_t control) {
	uint32_t index = cmd_ring_index;
	cmd_ring[index].ptr = ptr;
	cmd_ring[index].status = status;
	__asm__ volatile("" ::: "memory");

	uint32_t ctrl = control & ~1u;
	if (cmd_ring_pcs)
		ctrl |= 1;
	cmd_ring[index].control = ctrl;

	if (++cmd_ring_index >= 63) {
		cmd_ring[63].ptr = cmd_ring_phys;
		cmd_ring[63].status = 0;
		__asm__ volatile("" ::: "memory");
		uint32_t lc = (XHCI_TRB_LINK << 10) | (1u << 1);
		if (cmd_ring_pcs)
			lc |= 1;
		cmd_ring[63].control = lc;
		cmd_ring_index = 0;
		cmd_ring_pcs ^= 1;
	}

	__asm__ volatile("sfence" ::: "memory");
	doorbell_regs[0] = 0;
}

/* Event ring */
void XHCIModule::process_events(int index) {
	uintptr_t flags = irq_save();
	spin_acquire(&xhci_event_lock);

	int start = (index == -1) ? 0 : index;
	int end = (index == -1) ? (int)max_intrs : (index + 1);

	for (int i = start; i < end; i++) {
		if (i >= (int)max_intrs)
			break;

		while (true) {
			struct xhci_trb ev = event_ring[i][event_ring_index[i]];
			if ((ev.control & 1) != event_ring_pcs[i])
				break;

			uint8_t ev_type = (ev.control >> 10) & 0x3F;
			uint8_t comp_code = (ev.status >> 24) & 0xFF;

			bool is_sync = false;

			if (ev_type == XHCI_TRB_TRANSFER_EVENT) {
				uint8_t slot_id = (ev.control >> 24) & 0xFF;
				uint32_t ep_id = (ev.control >> 16) & 0x1F;
				if (ep_id > 1) {
					uint32_t ep_idx = ep_id - 1;
					size_t len = ev.status & 0xFFFFFF;
					bool is_err =
					    (comp_code != 1 && comp_code != 13);
					call_completion_callback(
					    ioforge_get_usb_devices_root(),
					    slot_id, ep_idx, len, is_err);
				} else {
					last_sync_ev = ev;
					has_sync_ev = true;
					is_sync = true;
					if (comp_code != 1)
						log(mod,
						    "[XHCI] EP0 error: code=%d",
						    comp_code);
				}
			} else if (ev_type ==
			           XHCI_TRB_COMMAND_COMPLETION_EVENT) {
				last_sync_ev = ev;
				has_sync_ev = true;
				is_sync = true;
				if (comp_code != 1)
					log(mod,
					    "[XHCI] Command error: code=%d",
					    comp_code);
			} else if (ev_type ==
			           XHCI_TRB_PORT_STATUS_CHANGE_EVENT) {
				uint32_t port_id = (ev.ptr >> 24) & 0xFF;
				log(mod, "[XHCI] HOTPLUG DETECTED on Port %d",
				    port_id);
				pending_hotplug_bitmap |= (1u << (port_id - 1));
			}

			if (++event_ring_index[i] >= 64) {
				event_ring_index[i] = 0;
				event_ring_pcs[i] ^= 1;
			}

			if (is_sync)
				break;
		}

		runtime_regs->ir[i].erdp =
		    (event_ring_phys[i] +
		     event_ring_index[i] * sizeof(struct xhci_trb)) |
		    (1u << 3);

		if (runtime_regs->ir[i].iman & (1u << 0)) {
			runtime_regs->ir[i].iman |= (1u << 0);
		}
	}

	spin_release(&xhci_event_lock);
	irq_restore(flags);
}

struct xhci_trb XHCIModule::wait_for_event(uint8_t type) {
	for (int t = 0; t < 2000; t++) {
		process_events();
		if (has_sync_ev) {
			uint8_t et = (last_sync_ev.control >> 10) & 0x3F;
			if (et == type) {
				has_sync_ev = false;
				return xhci_trb(last_sync_ev.ptr,
				                last_sync_ev.status,
				                last_sync_ev.control);
			}
			has_sync_ev = false;
		}
		IOUtils::sleep(1);
	}
	log(mod, "[XHCI] set_configurationt waiting for event type %d", type);
	return xhci_trb(0, 0, 0);
}

/* IRQ handler */
void XHCIModule::fireHandler(int index) {
	XHCIModule* m = XHCIModule::getInstance();
	if (!m || !m->op_regs)
		return;

	uint32_t sts = m->op_regs->usbsts;
	if (!(sts & XHCI_STS_EINT))
		return;

	m->op_regs->usbsts = XHCI_STS_EINT;

	int start = (index == -1) ? 0 : index;
	int end = (index == -1) ? (int)m->max_intrs : (index + 1);

	for (int i = start; i < end; i++) {
		if (i >= (int)m->max_intrs)
			break;

		if (!m->irq_driven) {
			m->runtime_regs->ir[i].iman |= (1u << 0);
			continue;
		}

		m->runtime_regs->ir[i].iman |= (1u << 0);
	}

	m->process_events(index);

	// m->handle_pending_hotplug();
}

void XHCIModule::handle_pending_hotplug() {
	if (pending_hotplug_bitmap == 0)
		return;

	for (uint32_t i = 0; i < 32; i++) {
		if (pending_hotplug_bitmap & (1u << i)) {
			uint32_t port_id = i + 1;
			volatile uint32_t* portsc =
			    (volatile uint32_t*)((uintptr_t)op_regs + 0x400);
			uint32_t status = portsc[i * 4];

			log(mod,
			    "[XHCI] HOTPLUG EXEC: Invoking probe_ports() for "
			    "Port %d. "
			    "PORTSC=0x%x (CCS=%d, CSC=%d, PED=%d)",
			    port_id, status, (status & (1u << 0)) ? 1 : 0,
			    (status & (1u << 17)) ? 1 : 0,
			    (status & (1u << 1)) ? 1 : 0);

			if (status & (1u << 17)) {
				// Clear CSC
				uint32_t rw = status & ~0x00FE0000;
				portsc[i * 4] = rw | (1u << 17);
			}

			if (status & (1u << 0)) { // If CCS is connected
				// Switch to polling mode
				op_regs->usbcmd &= ~XHCI_CMD_INTE;
				for (uint32_t j = 0; j < max_intrs; j++)
					runtime_regs->ir[j].iman &= ~(1u << 1);
				irq_driven = false;

				uint32_t speed = (status >> 10) & 0xF;
				log(mod,
				    "[HOTPLUG] Port %d: Connected (Speed %d)",
				    port_id, speed);

				if (!(status &
				      (1u << 1))) { // Only reset if PED == 0
					log(mod,
					    "[HOTPLUG] Port %d: Resetting...",
					    port_id);
					uint32_t rw = status & ~0x00FE0000;
					portsc[i * 4] = rw | (1u << 4); // PR
					while (portsc[i * 4] & (1u << 4))
						IOForge::IOUtils::sleep(1);

					status = portsc[i * 4];
					speed = (status >> 10) & 0xF;
					portsc[i * 4] = (1u << 21) |
					                (1u << 17); // PRC | CSC
				}

				uint8_t slot_id = enable_slot();
				if (slot_id) {
					log(mod,
					    "[HOTPLUG] Port %d: Slot %d "
					    "enabled "
					    "(speed %d)",
					    port_id, slot_id, speed);
					if (address_device(slot_id, port_id,
					                   speed)) {
						probe_ports();
					}
				}
			} else {
				log(mod, "[HOTPLUG] Port %d: Disconnected.",
				    port_id);
			}

			pending_hotplug_bitmap &= ~(1u << i);
			enable_irq_driven_mode();
		}
	}
}

extern "C" void xhci_fire_handler() { XHCIModule::fireHandler(-1); }
extern "C" void xhci_fire_handler_0() { XHCIModule::fireHandler(0); }
extern "C" void xhci_fire_handler_1() { XHCIModule::fireHandler(1); }
extern "C" void xhci_fire_handler_2() { XHCIModule::fireHandler(2); }
extern "C" void xhci_fire_handler_3() { XHCIModule::fireHandler(3); }

/* Control transfers (EP0, synchronous) */
void XHCIModule::send_async_with_response(uint8_t addr, uint8_t ep,
                                          uint64_t setup_data, size_t /*sz*/,
                                          uintptr_t resp_phys,
                                          size_t resp_size) {
	uint8_t slot_id = addr;
	if (!slots[slot_id].active)
		return;

	has_sync_ev = false;

	uint32_t ep_idx = ep; // EP0
	volatile struct xhci_trb* ring = slots[slot_id].rings[ep_idx];
	uint32_t idx = slots[slot_id].ring_indices[ep_idx];
	uint8_t pcs = slots[slot_id].ring_pcs[ep_idx];

	auto push = [&](uint64_t ptr, uint32_t status, uint32_t type, bool ioc,
	                uint32_t extra) {
		ring[idx].ptr = ptr;
		ring[idx].status = status;
		__asm__ volatile("sfence" ::: "memory");
		uint32_t c = (type << 10) | extra;
		if (ioc)
			c |= (1u << 5);
		if (pcs)
			c |= 1;
		ring[idx].control = c;

		if (++idx >= 63) {
			ring[63].ptr = slots[slot_id].rings_phys[ep_idx];
			ring[63].status = 0;
			__asm__ volatile("" ::: "memory");
			uint32_t lc = (XHCI_TRB_LINK << 10) | (1u << 1);
			if (pcs)
				lc |= 1;
			ring[63].control = lc;
			idx = 0;
			pcs ^= 1;
		}
	};

	bool is_in = (setup_data & 0x80) != 0;
	uint32_t trt = 0;
	if (resp_phys && resp_size) {
		trt = is_in ? (3u << 16) : (2u << 16);
	}

	uint32_t setup_extra = trt | (1u << 6); // IDT=1
	push(setup_data, 8, XHCI_TRB_SETUP_STAGE, false, setup_extra);

	if (resp_phys && resp_size) {
		uint32_t data_dir = is_in ? (1u << 16) : 0;
		push(resp_phys, resp_size, XHCI_TRB_DATA_STAGE, false,
		     data_dir);
	}

	uint32_t status_dir;
	if (resp_phys && resp_size) {
		status_dir = is_in ? 0 : (1u << 16); // Opposite of data stage
	} else {
		status_dir = (1u << 16); // No data stage -> Status is IN
	}
	push(0, 0, XHCI_TRB_STATUS_STAGE, true, status_dir);

	slots[slot_id].ring_indices[ep_idx] = idx;
	slots[slot_id].ring_pcs[ep_idx] = pcs;

	__asm__ volatile("" ::: "memory");
	doorbell_regs[slot_id] = ep_idx + 1;

	wait_for_event(XHCI_TRB_TRANSFER_EVENT);
}

/* Slot management */
uint8_t XHCIModule::enable_slot() {
	has_sync_ev = false;
	send_command(0, 0, (XHCI_TRB_ENABLE_SLOT_CMD << 10));
	struct xhci_trb ev = wait_for_event(XHCI_TRB_COMMAND_COMPLETION_EVENT);
	return (ev.control >> 24) & 0xFF;
}

bool XHCIModule::address_device(uint8_t slot_id, uint8_t port_id,
                                uint32_t speed) {
	has_sync_ev = false;

	//  Output (Device) Context: 64-byte aligned
	auto dctx = dma_alloc_aligned(context_size * 32, 64);
	slots[slot_id].ctx = dctx.vaddr;
	slots[slot_id].ctx_phys = dctx.paddr;
	slots[slot_id].port_id = port_id;
	slots[slot_id].active = true;
	dcbaa[slot_id] = (uint64_t)dctx.paddr;
	__asm__ volatile("sfence" ::: "memory");

	//  EP0 transfer ring: 64-byte aligned
	uintptr_t ring_paddr = 0;
	struct xhci_trb* ring = create_transfer_ring(&ring_paddr);
	slots[slot_id].rings[0] = ring;
	slots[slot_id].rings_phys[0] = ring_paddr;
	slots[slot_id].ring_indices[0] = 0;
	slots[slot_id].ring_pcs[0] = 1;

	// --- Input Context: 64-byte aligned ---
	auto ictx = dma_alloc_aligned(context_size * (2 + 32), 64);

	auto* ctrl_ctx = get_input_control_ctx(ictx.vaddr);
	serial2_printf("control ctx at 0x%lx\n",
	               ictx.paddr +
	                   ((uint64_t)ctrl_ctx - (uint64_t)ictx.vaddr));

	ctrl_ctx->add_flags = 0x3; // A0=Slot, A1=EP0
	ctrl_ctx->drop_flags = 0;

	auto* in_slot = get_input_slot_ctx(ictx.vaddr);
	serial2_printf("input slot at 0x%lx\n",
	               ictx.paddr + ((uint64_t)in_slot - (uint64_t)ictx.vaddr));

	in_slot->info = (1u << 27) | (speed << 20); // Context Entries=1, Speed
	in_slot->info2 = (port_id << 16); // Root Hub Port Number [23:16]

	uint32_t mps = 64;
	if (speed == 1 || speed == 2)
		mps = 8; // Low-speed / Full-speed
	else if (speed == 4)
		mps = 512; // Super-speed

	auto* in_ep0 = get_input_ep_ctx(ictx.vaddr, 0);
	in_ep0->info = 0;
	in_ep0->info2 =
	    (mps << 16) | (0u << 8) | (4u << 3) | (3u << 1); // Control=4
	in_ep0->trdp = (uint64_t)ring_paddr | 1;             // DCS=1
	in_ep0->info3 = 8; // Average TRB Length = 8, Max ESIT Payload = 0

	log(mod, "[XHCI] Port %d: EP0 info2=0x%x trdp=0x%lx", port_id,
	    in_ep0->info2, in_ep0->trdp);

	log(mod,
	    "[XHCI] Port %d: ADDRESS_DEVICE slot=%d speed=%d mps=%d "
	    "ctx_paddr=0x%x",
	    port_id, slot_id, speed, mps, (uint32_t)ictx.paddr);

	send_command((uint64_t)ictx.paddr, 0,
	             (XHCI_TRB_ADDRESS_DEVICE_CMD << 10) // TRB Type
	                 | (slot_id << 24));

	struct xhci_trb ev = wait_for_event(XHCI_TRB_COMMAND_COMPLETION_EVENT);

	uint8_t code = (ev.status >> 24) & 0xFF;
	serial2_printf("Port %d: ICTX phys adr at : 0x%lx\n", port_id,
	               ictx.paddr);
	IOUtils::DMAFree((void*)ictx.raw_paddr, ictx.raw_vaddr, ictx.raw_size);

	if (code != 1) {
		log(mod,
		    "[XHCI] Port %d: ADDRESS_DEVICE failed: slot=%d speed=%d "
		    "code=%d",
		    port_id, slot_id, speed, code);
		return false;
	}
	return true;
}

/* Transfer ring creation: ring base must be 64-byte aligned */
struct xhci_trb* XHCIModule::create_transfer_ring(uintptr_t* phys_out) {
	auto a = dma_alloc_aligned(sizeof(struct xhci_trb) * 64, 64);
	IOUtils::memset(a.vaddr, 0, sizeof(struct xhci_trb) * 64);

	auto* ring = (struct xhci_trb*)a.vaddr;
	ring[63].ptr = a.paddr;
	ring[63].status = 0;
	ring[63].control = (XHCI_TRB_LINK << 10) | (1u << 1) | 1;

	*phys_out = a.paddr;
	return ring;
}

/* Endpoint configuration */
// called from USBInterruptPipe
bool XHCIModule::configure_endpoint(uint8_t slot_id, uint8_t ep_address,
                                    uint8_t ep_type, uint16_t max_packet,
                                    uint8_t interval) {
	if (!slots[slot_id].active)
		return false;

	uint8_t ep_num = ep_address & 0xF;
	uint8_t is_in = (ep_address & 0x80) ? 1 : 0;
	uint32_t dci = (ep_num * 2) + is_in; // DCI: 1=EP0, 2=EP1OUT, 3=EP1IN
	uint32_t ep_idx = dci - 1;

	// Input Context: 64-byte aligned
	auto ictx = dma_alloc_aligned(context_size * 33, 64);

	void* dev_ctx = slots[slot_id].ctx;
	auto* slot = get_slot_ctx(dev_ctx);
	auto* in_slot = get_input_slot_ctx(ictx.vaddr);
	IOUtils::memcpy(in_slot, slot, sizeof(struct xhci_slot_ctx));

	get_input_control_ctx(ictx.vaddr)->add_flags = (1u << 0) | (1u << dci);
	get_input_control_ctx(ictx.vaddr)->drop_flags = 0;

	uint32_t max_dci = (slot->info >> 27) & 0x1F;
	if (dci > max_dci)
		in_slot->info = (in_slot->info & ~(0x1Fu << 27)) | (dci << 27);

	// New transfer ring: 64-byte aligned
	uintptr_t ring_paddr = 0;
	struct xhci_trb* ring = create_transfer_ring(&ring_paddr);

	uint32_t xhci_ep_type = 0;
	if (ep_type == 3)
		xhci_ep_type = is_in ? 7 : 6; // Interrupt IN/OUT
	else if (ep_type == 2)
		xhci_ep_type = is_in ? 5 : 4; // Bulk IN/OUT

	auto* in_ep = get_input_ep_ctx(ictx.vaddr, ep_idx);
	uint32_t xhci_interval = 3;
	uint32_t target_us = (interval == 0 ? 1 : interval) * 1000;
	while ((125u << xhci_interval) < target_us && xhci_interval < 15) {
		xhci_interval++;
	}

	in_ep->info = (xhci_interval << 16);

	uint32_t target_intr = next_interrupter_target % max_intrs;
	next_interrupter_target++;

	in_ep->info2 = ((uint32_t)max_packet << 16) | (target_intr << 8) |
	               (xhci_ep_type << 3) | (3u << 1);

	in_ep->info3 = ((uint32_t)max_packet << 16) | (uint32_t)max_packet;
	in_ep->trdp = (uint64_t)ring_paddr | 1;

	has_sync_ev = false;
	send_command((uint64_t)ictx.paddr, 0,
	             (XHCI_TRB_CONFIGURE_ENDPOINT_CMD << 10) | (slot_id << 24));
	struct xhci_trb ev = wait_for_event(XHCI_TRB_COMMAND_COMPLETION_EVENT);

	bool ok = ((ev.status >> 24) == 1);
	if (ok) {
		slots[slot_id].rings[ep_idx] = ring;
		slots[slot_id].rings_phys[ep_idx] = ring_paddr;
		slots[slot_id].ring_indices[ep_idx] = 0;
		slots[slot_id].ring_pcs[ep_idx] = 1;

		// Copy configured EP context back into device context
		IOUtils::memcpy(get_ep_ctx(dev_ctx, ep_idx), in_ep,
		                sizeof(struct xhci_endpoint_ctx));
	}

	IOUtils::DMAFree((void*)ictx.raw_paddr, ictx.raw_vaddr, ictx.raw_size);
	return ok;
}

/* Interrupt transfer queueing */
void XHCIModule::queue_interrupt_transfer(uint8_t slot_id, uint32_t ep_idx,
                                          uintptr_t data_phys, size_t size) {

	if (!slots[slot_id].active)
		return;

	volatile struct xhci_trb* ring = slots[slot_id].rings[ep_idx];
	uint32_t idx = slots[slot_id].ring_indices[ep_idx];
	uint8_t pcs = slots[slot_id].ring_pcs[ep_idx];

	ring[idx].ptr = data_phys;
	ring[idx].status = (uint32_t)size;
	__asm__ volatile("sfence" ::: "memory");
	uint32_t ctrl = (XHCI_TRB_NORMAL << 10) | (1u << 5); // IOC=1
	if (pcs)
		ctrl |= 1;
	ring[idx].control = ctrl;

	if (++idx >= 63) {
		uint32_t lc = (XHCI_TRB_LINK << 10) | (1u << 1); // TC=1
		if (pcs)
			lc |= 1;
		ring[63].ptr = slots[slot_id].rings_phys[ep_idx];
		ring[63].control = lc;
		idx = 0;
		slots[slot_id].ring_pcs[ep_idx] ^= 1;
	}

	slots[slot_id].ring_indices[ep_idx] = idx;
	__asm__ volatile("sfence" ::: "memory");
	doorbell_regs[slot_id] = ep_idx + 1;
}

/* Completion callbacks */
void XHCIModule::call_completion_callback(ioforge_device* dev, uint8_t slot_id,
                                          uint32_t ep_idx, size_t len,
                                          bool error) {
	if (!dev)
		return;

	if (dev->type == IOFORGE_USB_DEVICE) {
		auto* usb = (ioforge_usb_device*)dev;
		if (usb->addr == slot_id && usb->pipe) {
			XHCIPipe* pipe = (XHCIPipe*)usb->pipe;
			if (pipe->ep_idx_ == ep_idx)
				pipe->on_complete(nullptr, len, error);
		}
	}

	call_completion_callback(dev->first_child, slot_id, ep_idx, len, error);
	call_completion_callback(dev->next_sibling, slot_id, ep_idx, len,
	                         error);
}