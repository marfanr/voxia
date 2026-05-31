#ifndef __XHCI__XHCI_HPP__
#define __XHCI__XHCI_HPP__

#include "ioforge/ioforge_pci.hpp"
#include "ioforge/ioforge_usb.h"
#include <type.h>

#define XHCI_VENDOR_ID 0x1b36
#define XHCI_DEVICE_ID 0x000d // QEMU XHCI

// Capability Registers
struct xhci_cap_regs {
    uint8_t caplength;
    uint8_t reserved;
    uint16_t hciversion;
    uint32_t hcsparams1;
    uint32_t hcsparams2;
    uint32_t hcsparams3;
    uint32_t hccparams1;
    uint32_t dboff;
    uint32_t rtsoff;
    uint32_t hccparams2;
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
    struct xhci_intr_regs ir[1024]; // Max 1024 interrupters
} __attribute__((packed));

// Port Status and Control Register
#define XHCI_PORT_CCS (1 << 0)
#define XHCI_PORT_PED (1 << 1)
#define XHCI_PORT_PR  (1 << 4)
#define XHCI_PORT_PLS_MASK (0xF << 5)
#define XHCI_PORT_PP  (1 << 9)
#define XHCI_PORT_SPEED_MASK (0xF << 10)
#define XHCI_PORT_CSC (1 << 17)
#define XHCI_PORT_PRC (1 << 21)

// USBCMD bits
#define XHCI_CMD_RS (1 << 0)
#define XHCI_CMD_HCRST (1 << 1)
#define XHCI_CMD_INTE (1 << 2)

// USBSTS bits
#define XHCI_STS_HCH (1 << 0)
#define XHCI_STS_CNR (1 << 11)

// TRB types
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

struct xhci_trb {
    uint64_t ptr;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

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

struct xhci_device_ctx {
    struct xhci_slot_ctx slot;
    struct xhci_endpoint_ctx ep[31];
} __attribute__((packed));

struct xhci_input_control_ctx {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t reserved[6];
} __attribute__((packed));

struct xhci_input_ctx {
    struct xhci_input_control_ctx input_control;
    struct xhci_device_ctx device;
} __attribute__((packed));

class XHCIModule : public IOforgePCI {
public:
    XHCIModule();
    void load() override;
    void unload() override;

    static XHCIModule* getInstance();
    static void fireHandler();

    void set_controller(ioforge_usb_controller_service* ctrl) { controller = ctrl; }

    // Data Transfer API
    void send_async_with_response(uint8_t addr, uint8_t endpoint,
                                 uint32_t data_phys, size_t size,
                                 uint32_t response_phys, size_t response_size);

private:
    void reset_controller();
    void init_controller();
    void probe_ports();

    // Internal Ring Management
    void send_command(struct xhci_trb* trb);
    struct xhci_trb wait_for_event(uint8_t type);

    uint8_t enable_slot();
    void address_device(uint8_t slot_id, uint8_t port_id);

    struct xhci_trb* create_transfer_ring(uintptr_t* phys);

    ioforge_pci_device* device;
    struct xhci_cap_regs* cap_regs;
    struct xhci_op_regs* op_regs;
    struct xhci_runtime_regs* runtime_regs;
    volatile uint32_t* doorbell_regs;

    uint32_t num_slots;
    uint32_t num_ports;
    uint32_t max_intrs;

    uint64_t* dcbaa;
    uintptr_t dcbaa_phys;

    struct xhci_trb* cmd_ring;
    uintptr_t cmd_ring_phys;
    uint32_t cmd_ring_index;
    uint8_t cmd_ring_pcs;

    struct xhci_trb* event_ring;
    uintptr_t event_ring_phys;
    uint32_t event_ring_index;
    uint8_t event_ring_pcs;

    // Slot management
    struct xhci_slot {
        bool active;
        uint8_t port_id;
        struct xhci_device_ctx* ctx;
        uintptr_t ctx_phys;
        struct xhci_trb* rings[32]; // Endpoint rings
        uintptr_t rings_phys[32];
        uint32_t ring_indices[32];
        uint8_t ring_pcs[32];
    } slots[256];

    struct xhci_erst_entry {
        uint64_t ba;
        uint32_t size;
        uint32_t reserved;
    } __attribute__((packed))* erst;
    uintptr_t erst_phys;

    ioforge_usb_controller_service* controller;

    static XHCIModule instance;
};

#endif // __XHCI__XHCI_HPP__
