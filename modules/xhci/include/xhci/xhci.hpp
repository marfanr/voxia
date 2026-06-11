#ifndef __XHCI__XHCI_HPP__
#define __XHCI__XHCI_HPP__

#include "ioforge/ioforge_pci.hpp"
#include "ioforge/ioforge_usb.h"
#include <type.h>

#define XHCI_VENDOR_ID 0x1b36
#define XHCI_DEVICE_ID 0x000d // QEMU XHCI

// Capability Registers
struct xhci_cap_regs {
	volatile uint8_t caplength;
	volatile uint8_t reserved;
	volatile uint16_t hciversion;
	volatile uint32_t hcsparams1;
	volatile uint32_t hcsparams2;
	volatile uint32_t hcsparams3;
	volatile uint32_t hccparams1;
	volatile uint32_t dboff;
	volatile uint32_t rtsoff;
	volatile uint32_t hccparams2;
} __attribute__((packed));

// Operational Registers
struct xhci_op_regs {
	volatile uint32_t usbcmd;
	volatile uint32_t usbsts;
	volatile uint32_t pagesize;
	uint32_t reserved1[2];
	volatile uint32_t dnctrl;
	volatile uint64_t crcr;
	uint32_t reserved2[4];
	volatile uint64_t dcbaap;
	volatile uint32_t config;
} __attribute__((packed));

// Runtime Registers
struct xhci_intr_regs {
	volatile uint32_t iman;
	volatile uint32_t imod;
	volatile uint32_t erstsz;
	uint32_t reserved;
	volatile uint64_t erstba;
	volatile uint64_t erdp;
} __attribute__((packed));

struct xhci_runtime_regs {
	volatile uint32_t mfindex;
	uint32_t reserved[7];
	struct xhci_intr_regs ir[1024];
} __attribute__((packed));

// Port Status and Control Register bits
#define XHCI_PORT_CCS (1u << 0) // Current Connect Status (RO)
#define XHCI_PORT_PED                                                          \
	(1u << 1) // Port Enabled/Disabled (W1C — writing 1 DISABLES port)
#define XHCI_PORT_PR (1u << 4)         // Port Reset (RW)
#define XHCI_PORT_PLS_MASK (0xFu << 5) // Port Link State
#define XHCI_PORT_PP (1u << 9)         // Port Power (RW)
#define XHCI_PORT_SPEED_MASK (0xFu << 10)
#define XHCI_PORT_CSC (1u << 17) // Connect Status Change (W1C)
#define XHCI_PORT_PEC (1u << 18) // Port Enable/Disable Change (W1C)
#define XHCI_PORT_WRC (1u << 19) // Warm Port Reset Change (W1C)
#define XHCI_PORT_OCC (1u << 20) // Over-current Change (W1C)
#define XHCI_PORT_PRC (1u << 21) // Port Reset Change (W1C)
#define XHCI_PORT_PLC (1u << 22) // Port Link State Change (W1C)
#define XHCI_PORT_CEC (1u << 23) // Port Config Error Change (W1C)

// All W1C bits in PORTSC
#define XHCI_PORTSC_W1C_MASK                                                   \
	(XHCI_PORT_PED | XHCI_PORT_CSC | XHCI_PORT_PEC | XHCI_PORT_WRC |       \
	 XHCI_PORT_OCC | XHCI_PORT_PRC | XHCI_PORT_PLC | XHCI_PORT_CEC)

// USBCMD bits
#define XHCI_CMD_RS (1u << 0)
#define XHCI_CMD_HCRST (1u << 1)
#define XHCI_CMD_INTE (1u << 2)

// USBSTS bits
#define XHCI_STS_HCH (1u << 0)
#define XHCI_STS_EINT (1u << 3)
#define XHCI_STS_CNR (1u << 11)

// TRB types (Table 6-91)
#define XHCI_TRB_NORMAL 1
#define XHCI_TRB_SETUP_STAGE 2
#define XHCI_TRB_DATA_STAGE 3
#define XHCI_TRB_STATUS_STAGE 4
#define XHCI_TRB_LINK 6
#define XHCI_TRB_ENABLE_SLOT_CMD 9
#define XHCI_TRB_ADDRESS_DEVICE_CMD 11
#define XHCI_TRB_CONFIGURE_ENDPOINT_CMD 12
#define XHCI_TRB_TRANSFER_EVENT 32
#define XHCI_TRB_COMMAND_COMPLETION_EVENT 33
#define XHCI_TRB_PORT_STATUS_CHANGE_EVENT 34

// Figure 6-8 (465)
struct xhci_trb {
	uint64_t ptr; // input context (6.2.5)
	uint32_t status;
	uint32_t control;

	xhci_trb() = default;
	xhci_trb(uint64_t p, uint32_t s, uint32_t c)
	    : ptr(p), status(s), control(c) {}
	xhci_trb(const volatile xhci_trb& other) {
		ptr = other.ptr;
		status = other.status;
		control = other.control;
	}
	xhci_trb& operator=(const xhci_trb& other) {
		ptr = other.ptr;
		status = other.status;
		control = other.control;
		return *this;
	}
	xhci_trb& operator=(const volatile xhci_trb& other) {
		ptr = other.ptr;
		status = other.status;
		control = other.control;
		return *this;
	}
} __attribute__((packed));

// TODO: handle dynamic kalau context size nya 32 dan 64
//  yang sekarang unutk 32, sementara dari qemu 64
struct xhci_slot_ctx {
    uint32_t info;
	uint32_t info2;
	uint32_t reserved[2];
	uint32_t reserved2[4];
} __attribute__((packed));

struct xhci_endpoint_ctx {
	uint32_t info;
	uint32_t info2;
	uint64_t trdp;
	uint32_t info3;
	uint32_t reserved[3];
} __attribute__((packed));

struct xhci_input_control_ctx {
	uint32_t drop_flags;
	uint32_t add_flags;
	uint32_t reserved[6];
} __attribute__((packed));

struct xhci_erst_entry {
	uint64_t ba;
	uint32_t size;
	uint32_t reserved;
} __attribute__((packed));

class XHCIModule : public IOforgePCI {
      public:
	XHCIModule();
	void load() override;
	void unload() override;

	static XHCIModule* getInstance();
	static void fireHandler(int index = -1);
	
	uint32_t get_max_intrs() const { return max_intrs; }

	void set_controller(ioforge_usb_controller_service* ctrl) {
		controller = ctrl;
	}

	// Descriptor helpers
	void usb_get_descriptor(uint8_t addr, uint8_t type, uint8_t index,
	                        uint8_t len, uint8_t* data);
	void usb_get_string_descriptor(uint8_t addr, uint8_t index, char* data,
	                               size_t size);

	// Data transfer API
	void send_async_with_response(uint8_t addr, uint8_t endpoint,
	                              uint64_t setup_data, size_t size,
	                              uintptr_t response_phys,
	                              size_t response_size);

	bool configure_endpoint(uint8_t slot_id, uint8_t ep_address,
	                        uint8_t ep_type, uint16_t max_packet,
	                        uint8_t interval);
	void queue_interrupt_transfer(uint8_t slot_id, uint32_t ep_idx,
	                              uintptr_t data_phys, size_t size);
	void call_completion_callback(ioforge_device* dev, uint8_t slot_id,
	                              uint32_t ep_idx, size_t len, bool error);
	void process_events(int index = -1);

	// DEFERRED WORK (Marked for future Workqueue transition)
	void handle_pending_hotplug();

      private:
	void reset_controller();
	void init_controller();
	void probe_ports();
	void enable_irq_driven_mode(); // called after enumeration

	uint32_t pending_hotplug_bitmap;

	// Internal ring management
	void send_command(uint64_t ptr, uint32_t status, uint32_t control);
	struct xhci_trb wait_for_event(uint8_t type);

	uint8_t enable_slot();
	bool address_device(uint8_t slot_id, uint8_t port_id, uint32_t speed);
	struct xhci_trb* create_transfer_ring(uintptr_t* phys);

	inline void portsc_clear_bits(volatile uint32_t* reg,
	                              uint32_t w1c_bits) {
		uint32_t cur = *reg;
		uint32_t rw = cur & ~XHCI_PORTSC_W1C_MASK;
		*reg = rw | (w1c_bits & ~XHCI_PORT_PED & XHCI_PORTSC_W1C_MASK);
	}

	// Context accessors
	inline struct xhci_input_control_ctx*
	get_input_control_ctx(void* base) {
		return (struct xhci_input_control_ctx*)base;
	}
	inline struct xhci_slot_ctx* get_input_slot_ctx(void* base) {
		return (struct xhci_slot_ctx*)((uint8_t*)base + context_size);
	}
	inline struct xhci_endpoint_ctx* get_input_ep_ctx(void* base,
	                                                  int ep_idx) {
		return (struct xhci_endpoint_ctx*)((uint8_t*)base +
		                                   (ep_idx + 2) * context_size);
	}

	// Device Context accessors (no Control context prefix)
	inline struct xhci_slot_ctx* get_slot_ctx(void* base) {
		return (struct xhci_slot_ctx*)base;
	}
	inline struct xhci_endpoint_ctx* get_ep_ctx(void* base, int ep_idx) {
		return (struct xhci_endpoint_ctx*)((uint8_t*)base +
		                                   (ep_idx + 1) * context_size);
	}

	ioforge_pci_device* device;
	struct xhci_cap_regs* cap_regs;
	struct xhci_op_regs* op_regs;
	struct xhci_runtime_regs* runtime_regs;
	volatile uint32_t* doorbell_regs;

	uint32_t num_slots;
	uint32_t num_ports;
	uint32_t max_intrs;
	uint32_t context_size;
	uint32_t next_interrupter_target;

	volatile uint64_t* dcbaa;
	uintptr_t dcbaa_phys;

	volatile struct xhci_trb* cmd_ring;
	uintptr_t cmd_ring_phys;
	uint32_t cmd_ring_index;
	uint8_t cmd_ring_pcs;

	volatile struct xhci_trb* event_ring[4];
	uintptr_t event_ring_phys[4];
	uint32_t event_ring_index[4];
	uint8_t event_ring_pcs[4];

	struct xhci_erst_entry* erst[4];
	uintptr_t erst_phys[4];

	struct xhci_slot {
		bool active;
		uint8_t port_id;
		void* ctx;
		uintptr_t ctx_phys;
		volatile struct xhci_trb* rings[32];
		uintptr_t rings_phys[32];
		uint32_t ring_indices[32];
		uint8_t ring_pcs[32];
	} slots[256];

	ioforge_usb_controller_service* controller;

	static bool irq_driven;

	static XHCIModule instance;
};

#endif // __XHCI__XHCI_HPP__