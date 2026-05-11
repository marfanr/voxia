#ifndef __HAL__USB__EHCI_H__
#define __HAL__USB__EHCI_H__

#include <ioforge/ioforge_pci.h>
#include <type.h>

/*
 * EHCI Controller Structures
 */

/**
 * EHCI operation registers structure
 */
struct ehci_operation {
	volatile uint32_t usbcmd;	    /* USB Command Register */
	volatile uint32_t usbsts;	    /* USB Status Register */
	volatile uint32_t usbintr;	    /* USB Interrupt Enable Register */
	volatile uint32_t frindex;	    /* USB Frame Index Register */
	volatile uint32_t ctrldssegment;    /* 4G Segment Selector */
	volatile uint32_t periodiclistbase; /* Frame List Base Address */
	volatile uint32_t asynclistaddr;    /* Next Async List Address */
	volatile uint32_t reserved[9];	    /* Reserved */
	volatile uint32_t configflag;	    /* Configure Flag Register */
	volatile uint32_t portsc[];	    /* Port Status/Control Registers */
} __attribute__((packed));

/**
 * EHCI Queue Head structure
 */
struct ehci_queue_head {
	uint32_t qhlp;		     /* Queue Head Link Pointer */
	uint32_t ch;		     /* Endpoint Characteristics */
	uint32_t cap;		     /* Endpoint Capabilities */
	volatile uint32_t currentTD; /* Current TD Pointer */

	volatile uint32_t nextTD;	/* Next TD Pointer */
	volatile uint32_t altTD;	/* Alternate Next TD Pointer */
	volatile uint32_t token;	/* Token */
	volatile uint32_t buffer[5];	/* Buffer Pointers */
	volatile uint32_t extbuffer[5]; /* Extended Buffer Pointers */
};

/**
 * EHCI Queue Task Descriptor
 */
struct ehci_queue_task_descriptor {
	volatile uint32_t link;		/* Next QTD Pointer */
	volatile uint32_t altlink;	/* Alternate Next QTD Pointer */
	volatile uint32_t token;	/* QTD Token */
	volatile uint32_t buffer[5];	/* Buffer Page Pointers */
	volatile uint32_t extbuffer[5]; /* Extended Buffer Page Pointers */

	boolean_t used; /* Whether this QTD is in use */
	uint32_t next;	/* Next QTD in chain */
};

/*
 * EHCI Controller Commands and Status Bits
 */

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
	EHCI_Q_SELECT_ITD = (0 << 1),
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

/*
 * Function Declarations
 */

/**
 * Initialize the EHCI controller
 */
void ehci_init(struct ioforge_pci_device* bar);

/**
 * EHCI interrupt handler
 */
void ioforge_usb_ehci_interrupt();

#endif // __HAL__USB__EHCI_H__
