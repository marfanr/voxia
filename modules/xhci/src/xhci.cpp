#include "xhci/xhci.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "memory/kalloc.h"
#include "xhci/xhci_pipe.hpp"
#include <ioforge/ioforge_new.hpp>
#include <spinlock.h>
#include <usb.h>

// ---------------------------------------------------------------------------
// Static members
// ---------------------------------------------------------------------------
bool XHCIModule::irq_driven = false;

// ---------------------------------------------------------------------------
// IRQ save/restore
// ---------------------------------------------------------------------------
static inline uintptr_t xhci_irq_save() {
	uintptr_t flags;
	__asm__ volatile("pushfq  \n\t"
	                 "pop %0  \n\t"
	                 "cli     \n\t"
	                 : "=r"(flags)
	                 :
	                 : "memory");
	return flags;
}
static inline void xhci_irq_restore(uintptr_t flags) {
	__asm__ volatile("push %0 \n\t"
	                 "popfq   \n\t"
	                 :
	                 : "r"(flags)
	                 : "memory", "cc");
}

// ---------------------------------------------------------------------------
// DMA allocation helpers with guaranteed alignment
//
// xHCI spec alignment requirements:
//   DCBAA        : 64-byte aligned
//   Device Context : 64-byte aligned
//   Input Context  : 64-byte aligned
//   Command Ring   : 64-byte aligned
//   Event Ring     : 64-byte aligned
//   TRB (transfer) : 16-byte aligned  (ring base must be 64-byte aligned)
//   ERST           : 64-byte aligned
//   Scratchpad     : page-aligned (4096)
//
// Strategy: allocate (size + align - 1) extra bytes so we can always find an
// aligned region inside the allocation.  We store the ORIGINAL (unaligned)
// virtual and physical addresses in out-params so the caller can free them.
// ---------------------------------------------------------------------------

struct dma_alloc_result {
	void* vaddr;         // aligned virtual address  (use this for access)
	uintptr_t paddr;     // aligned physical address (use this for HW)
	void* raw_vaddr;     // original vaddr from DMAAlloc (use for DMAFree)
	uintptr_t raw_paddr; // original paddr from DMAAlloc (use for DMAFree)
	size_t raw_size;     // total bytes handed to DMAAlloc
};

static dma_alloc_result dma_alloc_aligned(size_t size, size_t align) {
	// align must be a power of two
	size_t raw_size = size + align - 1;
	uintptr_t raw_paddr = 0;
	void* raw_vaddr = IOForge::IOUtils::DMAAlloc(raw_size, &raw_paddr);
	IOForge::IOUtils::memset(raw_vaddr, 0, raw_size);

	uintptr_t aligned_paddr = (raw_paddr + align - 1) & ~(align - 1);
	uintptr_t offset = aligned_paddr - raw_paddr;
	void* aligned_vaddr = (void*)((uintptr_t)raw_vaddr + offset);

	if (((uint64_t)aligned_vaddr & (align - 1ULL)) != 0) {
		serial2_printf("dma aloc alignment %d failed\n", align);
	}

	return {aligned_vaddr, aligned_paddr, raw_vaddr, raw_paddr, raw_size};
}

// ---------------------------------------------------------------------------
// Event ring state
// ---------------------------------------------------------------------------
static struct xhci_trb last_sync_ev;
static volatile bool has_sync_ev = false;
static spinlock_t xhci_event_lock;

// ===========================================================================
// Descriptor helpers
// ===========================================================================

void XHCIModule::usb_get_descriptor(uint8_t addr, uint8_t type, uint8_t index,
                                    uint8_t len, uint8_t* data) {
	// Setup packet + data buffer in one 64-byte-aligned DMA block
	size_t total = sizeof(struct usb_setup_packet) + len;
	auto alloc = dma_alloc_aligned(total, 64);

	auto* cmd = (struct usb_setup_packet*)alloc.vaddr;
	cmd->bRequest = 0x06;
	cmd->bmRequestType = 0x80;
	cmd->wValue = (uint16_t)((type << 8) | index);
	cmd->wIndex = 0;
	cmd->wLength = len;

	uintptr_t data_phys = alloc.paddr + sizeof(usb_setup_packet);
	uint8_t* data_buf = (uint8_t*)alloc.vaddr + sizeof(usb_setup_packet);

	send_async_with_response(addr, 0, *(uint64_t*)cmd, sizeof(usb_setup_packet),
	                         data_phys, len);

	IOUtils::memcpy(data, data_buf, len);
	IOUtils::DMAFree((void*)alloc.raw_paddr, alloc.raw_vaddr,
	                 alloc.raw_size);
}

void XHCIModule::usb_get_string_descriptor(uint8_t addr, uint8_t index,
                                           char* data, size_t size) {
	uint8_t* buffer = (uint8_t*)kalloc(255);
	IOUtils::memset(buffer, 0, 255);
	usb_get_descriptor(addr, 0x3, index, 255, buffer);

	auto* str = (struct usb_string_descriptor*)buffer;
	if (str->bLength < 2) {
		data[0] = 0;
		IOUtils::free(buffer, 255);
		return;
	}

	size_t len = (str->bLength - 2) / 2;
	size_t j = 0;
	for (size_t i = 0; i < len && j < size - 1; i++) {
		uint16_t ch = str->wData[i];
		if (ch < 128)
			data[j++] = (char)ch;
	}
	data[j] = 0;
	IOUtils::free(buffer, 255);
}

// ===========================================================================
// Controller lifecycle
// ===========================================================================

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
		IOUtils::memset(a.vaddr, 0, sizeof(uint64_t) * (num_slots + 1));
		dcbaa = (volatile uint64_t*)a.vaddr;
		dcbaa_phys = a.paddr;
		op_regs->dcbaap = (uint64_t)a.paddr;
		// (raw pointers not saved; controller owns this for its
		// lifetime)
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
	auto max_int = (hcsparams >> 8) & 0x7FF;
	log(mod, "max interrupts : %d\n", max_int);

	auto esrt_max = (hcsparams2 >> 4) & 0xF;
	log(mod, "esrt max : %d\n", esrt_max);

	//  Event ring: 64-byte aligned
	{
		auto ea = dma_alloc_aligned(sizeof(struct xhci_trb) * 64, 64);
		IOUtils::memset(ea.vaddr, 0, sizeof(struct xhci_trb) * 64);
		event_ring = (volatile struct xhci_trb*)ea.vaddr;
		event_ring_phys = ea.paddr;
		event_ring_index = 0;
		event_ring_pcs = 1;

		// ERST: 64-byte aligned, one entry
		auto esta =
		    dma_alloc_aligned(sizeof(struct xhci_erst_entry), 64);
		erst = (struct xhci_erst_entry*)esta.vaddr;
		erst_phys = esta.paddr;

		erst[0].ba = ea.paddr;
		erst[0].size = 64;
		erst[0].reserved = 0;

		runtime_regs->ir[0].erstsz = 1;
		runtime_regs->ir[0].erdp = ea.paddr | (1u << 3); // EHB
		runtime_regs->ir[0].erstba = esta.paddr;
		// IE=1, clear IP — but INTE in USBCMD stays off until after
		// enumeration
		runtime_regs->ir[0].iman |= 3;
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
	while (true) {
		uint32_t c = event_ring[event_ring_index].control;
		if ((c & 1) != event_ring_pcs)
			break; // Ring is empty
		process_events();
		has_sync_ev = false; // Discard sync events outside wait_for_event
	}

	// Enable Interrupter Enable (INTE)
	op_regs->usbcmd |= XHCI_CMD_INTE;
	// Enable interrupts
	runtime_regs->ir[0].imod = 0; // Disable moderation to eliminate any possible QEMU timer delays
	runtime_regs->ir[0].iman |= (1u << 1); // IE
	
	if (op_regs->usbsts & XHCI_STS_EINT)
		op_regs->usbsts = XHCI_STS_EINT;
	runtime_regs->ir[0].iman |= (1u << 0); // clear IP
	log(mod, "IRQ-driven mode enabled");
}

// ===========================================================================
// Port enumeration
// ===========================================================================

void XHCIModule::probe_ports() {
	// Force polling mode — override whatever IoForge IRQ registration did
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

		uint32_t speed = (status >> 10) & 0xF;
		log(mod, "Port %d: Connected (Speed %d)", i + 1, speed);

		if (!(status & XHCI_PORT_PED)) {
			log(mod, "Port %d: Resetting...", i + 1);

			// Preserve only RW bits (strip all W1C), then set PR
			uint32_t rw = (*reg) & ~XHCI_PORTSC_W1C_MASK;
			*reg = rw | XHCI_PORT_PR;
			while (*reg & XHCI_PORT_PR)
				IOUtils::sleep(1);

			status = *reg;
			speed = (status >> 10) & 0xF;

			// Clear only PRC and CSC — never write PED=1 (disables
			// port)
			*reg = XHCI_PORT_PRC | XHCI_PORT_CSC;
		}

		uint8_t slot_id = enable_slot();
		if (!slot_id)
			continue;

		log(mod, "Port %d: Slot %d enabled (speed %d)", i + 1, slot_id,
		    speed);

		if (!address_device(slot_id, i + 1, 2))
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
			log(mod, "[XHCI] Failed to get descriptor for slot %d",
			    slot_id);
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
			uint16_t total_len =
			    ((usb_config_descriptor*)data)->wTotalLength;
			if (total_len > 0x1000)
				total_len = 0x1000;
			usb_get_descriptor(slot_id, 2, j, total_len, data);

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

// ===========================================================================
// Command ring
// ===========================================================================

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

// ===========================================================================
// Event ring
// ===========================================================================

void XHCIModule::process_events() {
	uintptr_t flags = xhci_irq_save();
	spin_acquire(&xhci_event_lock);

	while (true) {
		struct xhci_trb ev = event_ring[event_ring_index];
		if ((ev.control & 1) != event_ring_pcs)
			break;

		uint8_t ev_type = (ev.control >> 10) & 0x3F;
		uint8_t comp_code = (ev.status >> 24) & 0xFF;
		// log(mod, "EVENT: type=%d code=%d idx=%d", ev_type, comp_code,
		//     event_ring_index);

		uint32_t proc_idx = event_ring_index;
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
				    ioforge_get_usb_devices_root(), slot_id,
				    ep_idx, len, is_err);
			} else {
				last_sync_ev = ev;
				has_sync_ev = true;
				is_sync = true;
				if (comp_code != 1)
					log(mod, "[XHCI] EP0 error: code=%d",
					    comp_code);
			}
		} else if (ev_type == XHCI_TRB_COMMAND_COMPLETION_EVENT) {
			last_sync_ev = ev;
			has_sync_ev = true;
			is_sync = true;
			if (comp_code != 1)
				log(mod, "[XHCI] Command error: code=%d",
				    comp_code);
		}

		if (++event_ring_index >= 64) {
			event_ring_index = 0;
			event_ring_pcs ^= 1;
		}

		if (is_sync)
			break;
	}

	runtime_regs->ir[0].erdp =
	    (event_ring_phys + event_ring_index * sizeof(struct xhci_trb)) |
	    (1u << 3);

	spin_release(&xhci_event_lock);
	xhci_irq_restore(flags);
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
	log(mod, "[XHCI] Timeout waiting for event type %d", type);
	return xhci_trb(0, 0, 0);
}

// ===========================================================================
// IRQ handler
// ===========================================================================

void XHCIModule::fireHandler() {
	XHCIModule* m = XHCIModule::getInstance();
	if (!m || !m->op_regs)
		return;

	uint32_t sts = m->op_regs->usbsts;
	if (!(sts & XHCI_STS_EINT))
		return;

	// serial2_printf("fire handler\n");

	m->op_regs->usbsts = XHCI_STS_EINT; // W1C — clear EINT

	if (!irq_driven) {
		// Polling phase owns the event ring — just de-assert the IRQ line
		m->runtime_regs->ir[0].iman |= (1u << 0); // W1C — clear IP
		return;
	}

	m->runtime_regs->ir[0].iman |= (1u << 0); // W1C - clear IP BEFORE processing events
	m->process_events(); // This will clear EHB via ERDP update
}

extern "C" void xhci_fire_handler() { XHCIModule::fireHandler(); }

// ===========================================================================
// Control transfers (EP0, synchronous)
// ===========================================================================

void XHCIModule::send_async_with_response(uint8_t addr, uint8_t /*ep*/,
                                          uint64_t setup_data, size_t /*sz*/,
                                          uintptr_t resp_phys,
                                          size_t resp_size) {
	uint8_t slot_id = addr;
	if (!slots[slot_id].active)
		return;

	has_sync_ev = false;

	uint32_t ep_idx = 0; // EP0
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
	// Setup Stage TRB must NOT have CH=1 (xHCI 4.11.2.2)
	// if (resp_phys && resp_size) {
	// 	setup_extra |= (1u << 4); // CH=1 if there is a Data Stage
	// }
	push(setup_data, 8, XHCI_TRB_SETUP_STAGE, false, setup_extra);
	
	if (resp_phys && resp_size) {
		uint32_t data_dir = is_in ? (1u << 16) : 0;
		// Data Stage must not have CH=1 since Status Stage is next (per xHCI spec 4.11.2.2)
		// Wait, spec actually says Setup and all Data EXCEPT last have CH=1.
		// Since we only have 1 Data Stage TRB, it is the last, so CH=0.
		push(resp_phys, resp_size, XHCI_TRB_DATA_STAGE, false, data_dir);
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

// ===========================================================================
// Slot management
// ===========================================================================

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
	               ictx.paddr +
	                   ((uint64_t)in_slot - (uint64_t)ictx.vaddr));

	in_slot->info =
	    (1u << 27) | (speed << 20); // Context Entries=1, Speed
	in_slot->info2 = (port_id << 16);     // Root Hub Port Number [23:16]

	uint32_t mps = 64;
	if (speed == 1 || speed == 2)
		mps = 8; // Low-speed / Full-speed
	else if (speed == 4)
		mps = 512; // Super-speed

	auto* in_ep0 = get_input_ep_ctx(ictx.vaddr, 0);
	// DW0: Interval = 0
	in_ep0->info = 0;
	// DW1: MPS[31:16] | MaxBurstSize[15:8] | EPType[5:3] | CErr[2:1]
	in_ep0->info2 = (mps << 16) | (0u << 8) | (4u << 3) | (3u << 1); // Control=4
	in_ep0->trdp = (uint64_t)ring_paddr | 1; // DCS=1
	// DW4: Max ESIT Payload[31:16] | Average TRB Length[15:0]
	in_ep0->info3 = 8; // Average TRB Length = 8, Max ESIT Payload = 0

	log(mod, "EP0 info2=0x%x trdp=0x%lx", in_ep0->info2, in_ep0->trdp);

	log(mod,
	    "[XHCI] ADDRESS_DEVICE slot=%d port=%d speed=%d mps=%d "
	    "ctx_paddr=0x%x",
	    slot_id, port_id, speed, mps, (uint32_t)ictx.paddr);

	send_command((uint64_t)ictx.paddr, 0,
	             (XHCI_TRB_ADDRESS_DEVICE_CMD << 10) // TRB Type
	                 | (slot_id << 24));
	struct xhci_trb ev = wait_for_event(XHCI_TRB_COMMAND_COMPLETION_EVENT);

	uint8_t code = (ev.status >> 24) & 0xFF;
	serial2_printf("ICTX phys adr at : 0x%lx\n", ictx.paddr);
	// IOUtils::DMAFree((void*)ictx.raw_paddr, ictx.raw_vaddr, ictx.raw_size);

	if (code != 1) {
		log(mod,
		    "[XHCI] ADDRESS_DEVICE failed: slot=%d port=%d speed=%d "
		    "code=%d",
		    slot_id, port_id, speed, code);
		return false;
	}
	return true;
}

// ===========================================================================
// Transfer ring creation: ring base must be 64-byte aligned
// ===========================================================================

struct xhci_trb* XHCIModule::create_transfer_ring(uintptr_t* phys_out) {
	// Allocate with 64-byte alignment — TRB rings must be 64-byte aligned
	auto a = dma_alloc_aligned(sizeof(struct xhci_trb) * 64, 64);
	IOUtils::memset(a.vaddr, 0, sizeof(struct xhci_trb) * 64);

	// Link TRB at slot 63: toggle cycle, TC=1, initial PCS=1
	auto* ring = (struct xhci_trb*)a.vaddr;
	ring[63].ptr = a.paddr; // wraps back to start
	ring[63].status = 0;
	ring[63].control = (XHCI_TRB_LINK << 10) | (1u << 1) | 1; // TC=1, PCS=1

	*phys_out = a.paddr;
	return ring;
	// Note: raw_vaddr/raw_paddr not saved here — rings live for device
	// lifetime If you need to free them, store raw pointers in xhci_slot
	// too
}

// ===========================================================================
// Endpoint configuration
// ===========================================================================
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

	// DW0: Interval[23:16] | Mult[9:8] | EPState[2:0]
	in_ep->info = (xhci_interval << 16);
	
	// DW1: MaxPacketSize[31:16] | MaxBurstSize[15:8] | EPType[5:3] | CErr[2:1]
	// Max Burst Size must be 0 for Low/Full/High speed (unless bursting is explicitly supported)
	in_ep->info2 = ((uint32_t)max_packet << 16) | (0u << 8) |
	               (xhci_ep_type << 3) | (3u << 1);
	               
	// DW4: Max ESIT Payload[31:16] | Average TRB Length[15:0]
	in_ep->info3 = ((uint32_t)max_packet << 16) | (uint32_t)max_packet;
	in_ep->trdp = (uint64_t)ring_paddr | 1; // DCS=1

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

// ===========================================================================
// Interrupt transfer queueing
// ===========================================================================

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

// ===========================================================================
// Completion callbacks
// ===========================================================================

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