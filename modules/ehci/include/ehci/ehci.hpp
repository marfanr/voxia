#ifndef __EHCI__EHCI_HPP__
#define __EHCI__EHCI_HPP__

#include "ioforge/ioforge_pci.hpp"
#include "ioforge/ioforge_usb.h"
#include "type.h"

#define EHCI_VENDOR_ID 0x8086
#define EHCI_DEVICE_ID 0x24cd

struct ehci_operation {
	volatile uint32_t usbcmd; /* USB Command Register */
	volatile uint32_t usbsts; /* USB Status Register */
	/*
	This register enables and disables reporting of the corresponding interrupt to the software. When a bit is set 
and the corresponding interrupt is active, an interrupt is generated to the host. Interrupt sources that are 
disabled in this register still appear in the USBSTS to allow the software to poll for events. 
	*/
	volatile uint32_t usbintr; /* USB Interrupt Enable Register */
	/*
	This register is used by the host controller to index into the periodic frame list. The register updates every 
125 microseconds (once each micro-frame). Bits [N:3] are used to select a particular entry in the Periodic 
Frame List during periodic schedule execution. The number of bits used for the index depends on the size of 
the frame list as set by system software in the Frame List Size field in the USBCMD register (see Table 2-9). 
This register must be written as a DWord. Byte writes produce undefined results. This register cannot be 
written unless the Host Controller is in the Halted state as indicated by the HCHalted bit (USBSTS register 
Section 2.3.2). A write to this register while the Run/Stop bit is set to a one (USBCMD register, Section 
2.3.1) produces undefined results. Writes to this register also affect the SOF value. See Section 4.5 for 
details.
	*/
	volatile uint32_t frindex;
	volatile uint32_t ctrldssegment;    /* 4G Segment Selector */
	volatile uint32_t periodiclistbase; /* Frame List Base Address */
	volatile uint32_t asynclistaddr;    /* Next Async List Address */
	volatile uint32_t reserved[9];	    /* Reserved */
	volatile uint32_t configflag;	    /* Configure Flag Register */
	volatile uint32_t portsc[];	    /* Port Status/Control Registers */
};

struct ehci_queue_head {
	volatile uint32_t qhlp;	     /* Queue Head Link Pointer */
	volatile uint32_t ch;	     /* Endpoint Characteristics */
	volatile uint32_t cap;	     /* Endpoint Capabilities */
	volatile uint32_t currentTD; /* Current TD Pointer */

	volatile uint32_t nextTD;	/* Next TD Pointer */
	volatile uint32_t altTD;	/* Alternate Next TD Pointer */
	volatile uint32_t token;	/* Token */
	volatile uint32_t buffer[5];	/* Buffer Pointers */
	volatile uint32_t extbuffer[5]; /* Extended Buffer Pointers */

	// internal (batas yang diakses hardware)
};

typedef struct ehci_queue_head_node ehci_queue_head_node_t;
struct ehci_queue_head_node {
	struct ehci_queue_head* head;
	uint32_t physaddr;
	ehci_queue_head_node_t* next;
};

struct ehci_queue_task_descriptor {
	volatile uint32_t link;		/* Next QTD Pointer */
	volatile uint32_t altlink;	/* Alternate Next QTD Pointer */
	volatile uint32_t token;	/* QTD Token */
	volatile uint32_t buffer[5];	/* Buffer Page Pointers */
	volatile uint32_t extbuffer[5]; /* Extended Buffer Page Pointers */

	boolean_t used; /* Whether this QTD is in use */
	uint32_t next;	/* Next QTD in chain */

	// internal
	uint32_t physaddr;
} __attribute__((packed));

typedef struct ehci_queue_task_descriptor_node
	ehci_queue_task_descriptor_node_t;

struct ehci_queue_task_descriptor_node {
	struct ehci_queue_task_descriptor* task_descriptor;
	uint32_t physaddr;
	ehci_queue_task_descriptor_node_t* next;
};

//
class EHCIModule : public IOforgePCI {
      public:
	EHCIModule();
	void setup();
	void load();
	void unload();
	void reset_device();
	void stop_device();
	void start_device();
	void init_periodic();
	void start_periodic();
	void stop_periodic();
	void probe();
	void port_reset(int port);
	void sendAsync(uint32_t data_phys, size_t size);
	void
	send_async_with_response(uint8_t addr, uint32_t data_phys, size_t size,
				 uint32_t response, size_t response_size);
	void
	send_async_with_response2(uint8_t addr, uint32_t data_phys, size_t size,
				  uint32_t response, size_t response_size);
	void procces_async(ehci_queue_task_descriptor* qtd);
	void assign_address(int address);
	void usb_get_descriptor(uint8_t addr, uint8_t type, uint8_t index,
				uint8_t len, uint8_t* data);
	static void fireHandler();
	static EHCIModule* getInstance();
	void set_controller(ioforge_usb_controller_service* controller);
	void usb_get_string_descriptor(uint8_t addr, uint8_t index, char* data,
				       size_t size);
	void init_controller();

      private:
	ioforge_pci_service* device;
	ehci_operation* ehci_op;
	uint32_t* hcsparam;
	uint32_t* hccparam;
	boolean_t is_trasaction_is_running;

	ehci_queue_head* qh1;
	ehci_queue_head* qh2;

	uintptr_t qh1_paddr, qh2_paddr;
	uint32_t* framelist;

	// cache
	boolean_t retrieve_qh(ehci_queue_head_node_t* out);
	boolean_t retrieve_qtd(ehci_queue_task_descriptor_node_t* out);
	void store_qh(ehci_queue_head_node_t* in);
	void store_qtd(ehci_queue_task_descriptor_node_t* in);

	// qh utils
	void push_to_qh(ehci_queue_head_node_t* qh);
	void pop_from_qh(ehci_queue_head_node_t* qh);

	ioforge_usb_controller_service* controller;
};

/* Interrupt Control */
#define EHCI_INTERRUPT_ENABLE 0x1

/* USB Command Register Bits */
#define EHCI_CONTROLLER_START 1
#define EHCI_CONTROLLER_RESET (1 << 1)
#define EHCI_START_PERIODIC_SCHEDULE (1 << 4)
#define EHCI_START_ASYNC_SCHEDULE (1 << 5)

/* USB Status Register Bits */
#define EHCI_HC_HALTED_STATUS (1 << 12)

/* Schedule Enable Bits */
#define EHCI_PERIODIC_SCHEDULE_ENABLE (1 << 4)
#define EHCI_ASYNC_SCHEDULE_ENABLE (5 << 4)

/* Port Control Bits */
#define EHCI_PORT_ENABLED (1 << 2)
#define EHCI_PORT_RESET (1 << 8)

/* Frame List Interval Values */
#define EHCI_1_MICRO_FRAME (1 << 16)
#define EHCI_2_MICRO_FRAME (2 << 16)
#define EHCI_4_MICRO_FRAME (4 << 16)
#define EHCI_8_MICRO_FRAME (8 << 16)
#define EHCI_16_MICRO_FRAME (16 << 16)
#define EHCI_32_MICRO_FRAME (32 << 16)
#define EHCI_64_MICRO_FRAME (64 << 16)

/*
 * Queue Head Definitions
 */

/* Queue Head Link Pointer Bits */
#define EHCI_QUEUE_HEAD_TERMINATE 1
#define EHCI_QUEUE_HEAD_TOKEN_HALTED (1 << 6)

/* Queue Head Types */
#define EHCI_QUEUE_HEAD_TYPE_QTD (0 << 1)
#define EHCI_QUEUE_HEAD_TYPE_QH (1 << 1)
#define EHCI_QUEUE_HEAD_TYPE_SITD (2 << 1)
#define EHCI_QUEUE_HEAD_TYPE_FSTN (3 << 1)

/* Queue Head Capability Bits */
#define EHCI_QH_CAP_DTC (1 << 14)
#define EHCI_QH_CAP_HEAD_OF_RECLAMATION (1 << 15)
#define EHCI_QH_CAP_MAX_PACKET_LENGTH(x) (x << 16)
#define EHCI_QH_EPS_MASK (3 << 12)

/*
 * Queue Transfer Descriptor Definitions
 */

/* QTD Link Pointer Bits */
#define EHCI_QTD_TERMINATE 1

/* QTD Token Bits */
#define EHCI_QTD_TOKEN_LENGTH(l) (l << 16)
#define EHCI_QTD_TOKEN_DATA (1 << 31)

/* Queue Type Selector */
enum EHCI_Q_SELECT {
	EHCI_Q_SELECT_QTD = (0 << 1),
	EHCI_Q_SELECT_QH = (1 << 1),
	EHCI_Q_SELECT_SITD = (2 << 1),
	EHCI_Q_SELECT_FSTN = (3 << 1),
};

/* QTD PID Codes */
enum EHCI_QTD_TOKEN_PID {
	EHCI_QTD_TOKEN_PID_OUT = (0 << 8),
	EHCI_QTD_TOKEN_PID_IN = (1 << 8),
	EHCI_QTD_TOKEN_PID_SETUP = (2 << 8),
};

/* QTD Status Bits */
enum EHCI_QTD_TOKEN_STATUS {
	EHCI_QTD_TOKEN_STATUS_ACTIVE = (1 << 7),
	EHCI_QTD_TOKEN_STATUS_HALTED = (1 << 6),
	EHCI_QTD_TOKEN_STATUS_BUFFER_ERROR = (1 << 5),
	EHCI_QTD_TOKEN_STATUS_BABBLE_DETECTED = (1 << 4),
	EHCI_QTD_TOKEN_STATUS_TRANSACTION_ERROR = (1 << 3),
};

/* QTD Error Count Values */
enum EHCI_QTD_TOKEN_ERROR_COUNT {
	EHCI_QTD_TOKEN_ERROR_COUNT_0 = (0 << 10),
	EHCI_QTD_TOKEN_ERROR_COUNT_1 = (1 << 10),
	EHCI_QTD_TOKEN_ERROR_COUNT_2 = (2 << 10),
	EHCI_QTD_TOKEN_ERROR_COUNT_3 = (3 << 10),
};

/* Queue Head Capability Multiplier */
enum EHCI_QH_CAP_MULT {
	EHCI_QH_CAP_MULT_1 = (1 << 30),
	EHCI_QH_CAP_MULT_2 = (2 << 30),
	EHCI_QH_CAP_MULT_3 = (3 << 30),
};

struct usb_setup_packet {
	uint8_t bmRequestType;
	uint8_t bRequest;
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};

enum usb_setup_packet_request {
	USB_SETUP_PACKET_GET_STATUS = 0,
	USB_SETUP_PACKET_CLEAR_FEATURE = 1,
	USB_SETUP_PACKET_SET_FEATURE = 3,
	USB_SETUP_PACKET_SET_ADDRESS = 5,
};

#endif //__EHCI__EHCI_HPP__