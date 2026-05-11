#include "./ehci.h"
#include "./packet.h"
#include "ioforge/ioforge_pci.h"
#include "libk/type.h"
#include <hal/cpu/paging.h>
#include <libk/console/console.h>
#include <libk/debug/debug.h>
#include <libk/serial.h>
#include <str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/slab.h>
#include <vfs/vfs.h>

#define ALIGN(ptr, x) (((uintptr_t) ptr + (x - 1)) & ~(x - 1))

/* Controller management */
static void ehci_stop_engine(struct ehci_operation* op);
static void ehci_reset_engine(struct ehci_operation* op);
static void ehci_start_engine(struct ehci_operation* op);
static void ehci_init_que_head(struct ehci_operation* op);
static void ehci_enable_periodic_schedule(struct ehci_operation* op);
static void ehci_disable_periodic_schedule(struct ehci_operation* op);
static void ehci_port_reset(struct ehci_operation* op, int port);
static void ehci_probe_all_ports(int ports, struct ehci_operation* op);

/* USB device operations */
static void ehci_set_address(uint8_t address, struct ehci_operation* op);
static void ehci_proccess_async(struct ehci_operation* op,
				struct ehci_queue_task_descriptor* qtd);
static void
ehci_send_packet_and_receive(struct ehci_operation* op, uint8_t addr,
			     struct usb_setup_packet* packet, void* data,
			     uint8_t endpoint);
static void usb_read_in(struct ehci_operation* op, uint8_t addr,
			uint32_t length, uint8_t endpoint, void* data);
static void* usb_get_descriptor(struct ehci_operation* op, uint8_t addr,
				uint8_t type, uint8_t index, uint8_t len);

/* Interface implementations */
// block_device_operations_t* ehci_block_impl();
// vfs_operations_t* ehci_hid_vfs_impl();
// uint8_t* ehci_block_read(void* this, uint64_t offset, size_t _count);
// struct vfs_open_response* ehci_vfs_open(block_device_operations_t* block_op,
//                                         const char* path, int _inode);

/* Helper functions */
static void* ehci_alloc(size_t size);
char scancode_to_char(uint8_t scancode, uint8_t modifiers);

// global variabel
static uint8_t* in_data = 0;
static uint32_t* framelist = 0;
static struct ehci_operation* op = 0;
static struct ehci_queue_task_descriptor* inttd = 0;
static uint8_t* vfs_data = 0;
static boolean_t is_trasaction_is_running = 0;
static boolean_t is_periodic_transaction_is_running = 0;

static boolean_t has_new_data = 0;

/* Slab allocator */
struct slab_cache* ehci_qh_cache;

/* Queue heads */
static struct ehci_queue_head* inhead;
static struct ehci_queue_head* inhead2;

/**
 * Allocates memory with proper alignment for EHCI structures
 * TODO: make some propper alloc for physical address
 *
 * @param size Size in bytes to allocate
 * @return Pointer to allocated memory
 */
static void* ehci_alloc(size_t size) {
	size_t s = 1 + (size / 4096);
	void* a = (void*) VIRT2PHYS(vxPhysBaseAlloc(s));

	vxMultipleMmap(
		(page_t) PHYS2VIRT((uintptr_t) paging_get_highest_page_map()),
		(uintptr_t) a, (uintptr_t) a, s, 0x3);
	paging_reload(paging_get_highest_page_map());
	paging_add_dma_mapping((uintptr_t) a, (uintptr_t) a,
			       s); // add to mapping data

	return a;
}

/* -----------------------------------
 * INITIALIZATION FUNCTIONS
 * -----------------------------------*/
/**
 * Initializes the EHCI controller
 *
 * @param device PCI device information
 */
void ehci_init(struct ioforge_pci_device* device) {
	serial_trace("EHCI: init\n");

	uintptr_t bar = device->bar[0].address;
	uintptr_t addr = 0xFFFF8FFF00300000;
	vxMultipleMmap(
		(page_t) PHYS2VIRT((uintptr_t) paging_get_highest_page_map()),
		addr, bar, 3, 0b111);
	paging_reload(paging_get_highest_page_map());
	paging_add_dma_mapping(bar, addr, 3);	// add to mapping data
	uint64_t offset = bar - (bar & ~0xFFF); // offset from 0xA0000

	// replace bar with mapped address
	bar = addr + offset;

	serial_trace("EHCI: bar %x\n", bar);
	uint8_t cap_length = *(uint8_t*) (bar);
	op = (struct ehci_operation*) (bar + cap_length);
	// uint32_t *port0    = (uint32_t *)(bar + cap_length + 0x44);
	uint32_t* hcsparam = (uint32_t*) (bar + 0x4);
	uint32_t* hccparam = (uint32_t*) (bar + 0x8);

	// is controller supported 64bit
	serial_trace("EHCI is 64 bit : %d\n", *hccparam & 1);

	ehci_stop_engine(op);
	ehci_reset_engine(op);

	op->frindex = 0;
	op->ctrldssegment = 0;
	// op->usbsts = 0x3f;

	op->usbcmd |= EHCI_1_MICRO_FRAME | (0b00 << 2);

	ehci_start_engine(op);

	// until OHCI is implemented, we will use this as a flag
	op->configflag = 1;

	int ports = *hcsparam & 0b1111;
	serial_trace("EHCI: port available : %d \n", ports);

	// prepare for migrate to slab
	serial_trace("EHCI: slab allocator cache size %d\n",
		     sizeof(struct slab_cache));

	inhead = (struct ehci_queue_head*) ehci_alloc(
		sizeof(struct ehci_queue_head));
	inhead2 = (struct ehci_queue_head*) ehci_alloc(
		sizeof(struct ehci_queue_head));

	ehci_init_que_head(op);
	serial_trace("EHCI: init qh done\n");

	op->usbsts = 0x3f;
	op->usbintr = 0x1F;

	in_data = (uint8_t*) ehci_alloc(4096);

	inttd = (struct ehci_queue_task_descriptor*) ehci_alloc(
		sizeof(struct ehci_queue_task_descriptor));
	vfs_data = (uint8_t*) ehci_alloc(4096);
	memset(vfs_data, 0, 4096);

	// vfs setup
	// block_register_device("/block/ehci", ehci_block_impl(), 0);
	// vfs_register_fs("ehci_hid", ehci_hid_vfs_impl(), 0);
	// vfs_mount("/dev/hid0", "/block/ehci", "ehci_hid");

	ehci_probe_all_ports(ports, op);

	serial_trace("EHCI: init done\n\n");
}

/* -----------------------------------
 * CONTROLLER MANAGEMENT FUNCTIONS
 * -----------------------------------*/
/**
 * Stops the EHCI controller engine
 *
 * @param op Pointer to EHCI operation registers
 */
static void ehci_stop_engine(struct ehci_operation* op) {
	serial_trace("EHCI: stopping_engine\n");
	op->usbcmd &= ~(1 << 4);
	op->usbcmd &= ~(1 << 5);

	while (op->usbsts & ((1 << 14) | (1 << 15)))
		;

	serial_trace("EHCI: stop_engine\n");

	op->usbcmd &= ~1;
	while (!(op->usbsts & (1 << 12)))
		;
	serial_trace("EHCI: stop_engine\n");
}

/**
 * Resets the EHCI controller engine
 *
 * @param op Pointer to EHCI operation registers
 */
static void ehci_reset_engine(struct ehci_operation* op) {
	op->usbcmd |= EHCI_CONTROLLER_RESET;
	while (op->usbcmd & EHCI_CONTROLLER_RESET) {
		if (op->usbsts & (1 << 4)) {
			serial_trace("EHCI: reset failed\n");
			break;
		}
	}
	serial_trace("EHCI: reset engine\n");
}

/**
 * Starts the EHCI controller engine
 *
 * @param op Pointer to EHCI operation registers
 */
static void ehci_start_engine(struct ehci_operation* op) {
	op->usbcmd |= EHCI_CONTROLLER_START;
	while ((op->usbsts & EHCI_HC_HALTED_STATUS))
		;
	serial_trace("EHCI: start engine\n");
}

/**
 * Initializes queue heads for periodic transfers
 *
 * @param op Pointer to EHCI operation registers
 */
static void ehci_init_que_head(struct ehci_operation* op) {
	serial_trace("EHCI: init_que_head\n");
	struct ehci_queue_head* qh = (struct ehci_queue_head*) ehci_alloc(
		sizeof(struct ehci_queue_head));

	serial_trace("qh : 0x%x \n", qh);

	// init framelist
	framelist = (uint32_t*) ehci_alloc(1024 * sizeof(uint32_t));
	// memset (framelist, 0, 1024 * sizeof (uint32_t));

	qh->altTD = 1;
	qh->nextTD = 1;
	qh->qhlp = 1;
	qh->currentTD = 0;
	qh->ch = 0;
	qh->token = 0x40;

	for (int i = 0; i < 1024; i++) {
		framelist[i] = ((uint32_t) qh) | (1 << 1);
	}

	serial_trace("framelist : 0x%x \n", framelist);
	op->frindex = 0;
	op->periodiclistbase = (uint32_t) framelist;
	op->usbcmd |= (1 << 4);
}

/**
 * Enables periodic schedule in EHCI controller
 *
 * @param op Pointer to EHCI operation registers
 */
static void ehci_enable_periodic_schedule(struct ehci_operation* op) {
	op->usbcmd |= EHCI_PERIODIC_SCHEDULE_ENABLE;
}

/**
 * Disables periodic schedule in EHCI controller
 *
 * @param op Pointer to EHCI operation registers
 */
static void ehci_disable_periodic_schedule(struct ehci_operation* op) {
	op->usbcmd &= ~EHCI_PERIODIC_SCHEDULE_ENABLE;
}

/**
 * Resets a specific EHCI port
 *
 * @param op Pointer to EHCI operation registers
 * @param port Port number to reset
 */
static void ehci_port_reset(struct ehci_operation* op, int port) {
	op->portsc[port] |= EHCI_PORT_RESET;
	serial_trace("EHCI: resetting port %d\n", port);
	// //usleep(1000);
	op->portsc[port] &= ~EHCI_PORT_RESET;
}

/* -----------------------------------
 * USB TRANSACTION FUNCTIONS
 * -----------------------------------*/
/**
 * Processes an asynchronous transfer
 *
 * @param op Pointer to EHCI operation registers
 * @param qtd Queue Task Descriptor to process
 */
static void ehci_proccess_async(struct ehci_operation* op,
				struct ehci_queue_task_descriptor* qtd) {
	op->usbcmd |= EHCI_START_ASYNC_SCHEDULE;
	// //usleep(100000);

	// for (int i = 0; i < 100; i++) {
	// usleep(100000);
	while (is_trasaction_is_running) {
		if (qtd->token & (1 << 6)) {
			serial_trace("halted\n");
			goto proccess_end;
			break;
		} else if (qtd->token & (1 << 5)) {
			serial_trace("Data Buffer Error\n");
			goto proccess_end;
			break;
		} else if (qtd->token & (1 << 4)) {
			serial_trace("Babble detected\n");
			goto proccess_end;
			break;
		} else if (qtd->token & (1 << 3)) {
			serial_trace("Transaction error\n");
			goto proccess_end;
			break;
		} else if (qtd->token & (1 << 2)) {
			serial_trace("Buffer error\n");
			goto proccess_end;
			break;
		}
	}
proccess_end:
	op->usbcmd &= ~EHCI_START_ASYNC_SCHEDULE;
}

/**
 * Sets USB device address
 *
 * @param address USB device address to set
 * @param op Pointer to EHCI operation registers
 */
static void ehci_set_address(uint8_t address, struct ehci_operation* op) {
	struct usb_setup_packet* cmd = (struct usb_setup_packet*) ehci_alloc(
		sizeof(struct usb_setup_packet));
	serial_trace("EHCI:cmd 0x%x\n", cmd);

	cmd->bmRequestType = 0;
	cmd->bRequest = USB_SETUP_PACKET_SET_ADDRESS;
	cmd->bmRequestType = 0;
	cmd->wValue = address;
	cmd->wIndex = 0;
	cmd->wLength = 0;

	// resetting address
	struct ehci_queue_head* head = (struct ehci_queue_head*) ehci_alloc(
		sizeof(struct ehci_queue_head));

	struct ehci_queue_head* head2 = (struct ehci_queue_head*) ehci_alloc(
		sizeof(struct ehci_queue_head));

	struct ehci_queue_task_descriptor* setup =
		(struct ehci_queue_task_descriptor*) ehci_alloc(
			sizeof(struct ehci_queue_task_descriptor));

	struct ehci_queue_task_descriptor* status =
		(struct ehci_queue_task_descriptor*) ehci_alloc(
			sizeof(struct ehci_queue_task_descriptor));

	setup->link = (uint32_t) status;
	setup->altlink = EHCI_QTD_TERMINATE;
	setup->token = EHCI_QTD_TOKEN_LENGTH(sizeof(struct usb_setup_packet));
	setup->token |= EHCI_QTD_TOKEN_PID_SETUP;
	setup->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
	setup->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
	setup->buffer[0] = (uint32_t) cmd;

	status->altlink = EHCI_QTD_TERMINATE;
	status->link = EHCI_QTD_TERMINATE;
	status->token = EHCI_QTD_TOKEN_PID_IN;
	status->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE;
	status->token |= EHCI_QTD_TOKEN_ERROR_COUNT_3;
	status->token |= EHCI_QTD_TOKEN_DATA;
	status->token |= (1 << 15);

	head2->altTD = EHCI_QTD_TERMINATE;
	head2->nextTD = (uint32_t) setup;
	head2->qhlp = ((uint32_t) head) | EHCI_Q_SELECT_QH;
	head2->currentTD = 0;
	head2->ch |= EHCI_QH_CAP_DTC;
	head2->ch |= EHCI_QH_CAP_MAX_PACKET_LENGTH(16);
	head2->cap = EHCI_QH_CAP_MULT_1;

	head->qhlp = (uint32_t) head2 | EHCI_Q_SELECT_QH;
	head->altTD = EHCI_QTD_TERMINATE;
	head->nextTD = EHCI_QTD_TERMINATE;
	head->currentTD = 0;
	head->ch |= EHCI_QH_CAP_HEAD_OF_RECLAMATION | 0;
	op->asynclistaddr = (uint32_t) head;

	is_trasaction_is_running = 1;
	ehci_proccess_async(op, status);

	serial_trace("EHCI: set address  %d\n", address);
}

/**
 * Sends a USB packet and receives response
 *
 * @param op Pointer to EHCI operation registers
 * @param addr USB device address
 * @param packet USB setup packet to send
 * @param data Buffer for received data
 * @param endpoint USB endpoint number
 */
static void
ehci_send_packet_and_receive(struct ehci_operation* op, uint8_t addr,
			     struct usb_setup_packet* packet, void* data,
			     uint8_t endpoint) {
	struct ehci_queue_task_descriptor* cmd =
		(struct ehci_queue_task_descriptor*) ehci_alloc(
			sizeof(struct ehci_queue_task_descriptor));

	struct ehci_queue_task_descriptor* td =
		(struct ehci_queue_task_descriptor*) ehci_alloc(
			sizeof(struct ehci_queue_task_descriptor));

	struct ehci_queue_task_descriptor* status =
		(struct ehci_queue_task_descriptor*) ehci_alloc(
			sizeof(struct ehci_queue_task_descriptor));

	cmd->altlink = 1;
	cmd->token |= (8 << 16);   // cmd size
	cmd->token |= (1 << 7);	   // actief
	cmd->token |= (0x2 << 8);  // type is cmd
	cmd->token |= (0x3 << 10); // maxerror
	cmd->buffer[0] = (uint32_t) packet;

	cmd->link = (uint32_t) td;
	td->altlink = 1;
	td->link = (uint32_t) status;
	td->token |= (packet->wLength << 16); // td size
	td->token |= (1 << 7);		      // actief
	td->token |= (1 << 8);		      // type is td
	td->token |= (0x3 << 10);	      // maxerror
	td->token |= (1 << 31);		      // data
	td->buffer[0] = (uint32_t) data;

	status->altlink = 1;
	status->link = 1;
	status->token |= (1 << 7);    // actief
	status->token |= (0 << 8);    // type is td
	status->token |= (0x3 << 10); // maxerror
	status->token |= (1 << 31);   // data
	status->token |= (1 << 15);

	struct ehci_queue_head* qh = (struct ehci_queue_head*) ehci_alloc(
		sizeof(struct ehci_queue_head));
	struct ehci_queue_head* qh2 = (struct ehci_queue_head*) ehci_alloc(
		sizeof(struct ehci_queue_head));

	qh2->altTD = 1;
	qh2->qhlp = ((uint32_t) qh) | 2;
	qh2->nextTD = (uint32_t) cmd;
	qh2->currentTD = 0;
	qh2->ch |= 1 << 14;   // dtc
	qh2->ch |= 512 << 16; // mplen
	qh2->ch |= 2 << 12;   // eps
	qh2->ch |= addr;      // eps
	if (endpoint != 0)
		qh2->ch |= (endpoint << 8);
	qh2->cap = 0x40000000;

	qh->nextTD = 1;
	qh->altTD = 1;
	qh->qhlp = ((uint32_t) qh2) | 2;
	qh->currentTD = 0;
	qh->ch = 1 << 15; // T
	qh->token = 0x40;

	is_trasaction_is_running = 1;
	op->asynclistaddr = (uint32_t) (uintptr_t) qh;
	ehci_proccess_async(op, status);
}

/**
 * Gets USB descriptor from a device
 *
 * @param op Pointer to EHCI operation registers
 * @param addr USB device address
 * @param type Descriptor type
 * @param index Descriptor index
 * @param len Length of descriptor to request
 * @return Pointer to descriptor buffer
 */
static void* usb_get_descriptor(struct ehci_operation* op, uint8_t addr,
				uint8_t type, uint8_t index, uint8_t len) {
	struct usb_setup_packet* cmd = (struct usb_setup_packet*) ehci_alloc(
		sizeof(struct usb_setup_packet));
	// serial_trace ("EHCI:cmd 0x%x\n", cmd);
	// memset (cmd, 0, sizeof (struct usb_setup_packet));
	cmd->bRequest = 0x06;
	cmd->bmRequestType = 0x80; // recieve
	cmd->wValue = (type << 8) | index;
	cmd->wIndex = 0;
	cmd->wLength = len;
	// serial_trace ("EHCI: get descriptor %d\n", type);

	struct ehci_queue_task_descriptor* setup =
		(struct ehci_queue_task_descriptor*) ehci_alloc(
			sizeof(struct ehci_queue_task_descriptor));

	struct ehci_queue_task_descriptor* data =
		(struct ehci_queue_task_descriptor*) ehci_alloc(
			sizeof(struct ehci_queue_task_descriptor));

	struct ehci_queue_task_descriptor* status =
		(struct ehci_queue_task_descriptor*) ehci_alloc(
			sizeof(struct ehci_queue_task_descriptor));

	uint8_t* it = (uint8_t*) ehci_alloc(len);

	setup->link = (uint32_t) data;
	setup->altlink = EHCI_QTD_TERMINATE;
	setup->token |= EHCI_QTD_TOKEN_LENGTH(8);     // setup size
	setup->token |= EHCI_QTD_TOKEN_STATUS_ACTIVE; // actief
	setup->token |= (0x2 << 8);		      // type is setup
	setup->token |= (0x3 << 10);		      // maxerror
	setup->buffer[0] = (uint32_t) cmd;

	data->link = (uint32_t) status;
	data->altlink = EHCI_QTD_TERMINATE;
	data->token |= (len << 16); // setup size
	data->token |= (1 << 7);    // aktif
	data->token |= (1 << 31);   // toggle
	data->token |= (0x1 << 8);  // type is in
	data->token |= (0x3 << 10); // maxerror
	data->buffer[0] = (uint32_t) it;

	status->link = 1;
	status->altlink = 1;
	status->token |= (0 << 8);    // PID instellen
	status->token |= (1 << 31);   // toggle
	status->token |= (1 << 7);    // actief
	status->token |= (0x3 << 10); // maxerror
	status->token |= (1 << 15);

	struct ehci_queue_head* head = (struct ehci_queue_head*) ehci_alloc(
		sizeof(struct ehci_queue_head));

	struct ehci_queue_head* head2 = (struct ehci_queue_head*) ehci_alloc(
		sizeof(struct ehci_queue_head));

	head2->altTD = 1;
	head2->nextTD = (uint32_t) setup; // qdts2
	head2->qhlp = ((uint32_t) head) | 2;
	head2->currentTD = 0;  // qdts1
	head2->ch |= 1 << 14;  // dtc
	head2->ch |= 64 << 16; // mplen
	head2->ch |= addr;     // eps
	head2->cap = 0x40000000;

	//
	// Eerste commando
	head->altTD = 1;
	head->nextTD = 1;
	head->qhlp = ((uint32_t) head2) | 2;
	head->currentTD = 0;
	head->ch = 1 << 15; // T

	op->asynclistaddr = (uint32_t) (uintptr_t) head;

	is_trasaction_is_running = 1;
	ehci_proccess_async(op, status);

	return it;
}

static void usb_read_in(struct ehci_operation* op, uint8_t addr,
			uint32_t length, uint8_t endpoint, void* data) {
	// memset (inttd, 0, sizeof (struct ehci_queue_task_descriptor));
	inttd->altlink = 1;		// end link
	inttd->link = 1;		// end link
	inttd->token |= (length << 16); // buffer length
	inttd->token |= (1 << 7);	// active toggle
	inttd->token |= (1 << 8);	// type is Transaction Descriptor
	inttd->token |= (0x3 << 10);	// max 3x retry on error
	inttd->token |= (0 << 31);	// data toggle
	inttd->token |= (1 << 15);	// Interrupt On Complete
	inttd->buffer[0] = (uint32_t) data;

	// memset (inhead2, 0, sizeof (struct ehci_queue_head));

	inhead2->altTD = (uint32_t) inttd;
	inhead2->qhlp = ((uint32_t) inhead) | 2;
	inhead2->nextTD = (uint32_t) inttd;
	inhead2->currentTD = 0;
	inhead2->ch |= 1 << 14;	  // dtc
	inhead2->ch |= 512 << 16; // mplen
	inhead2->ch |= 2 << 12;	  // eps
	inhead2->ch |= 1 << 8;	  // eps
	inhead2->ch |= addr;	  // eps
	inhead2->cap = 0x40000000 | 1;

	inhead->nextTD = 1;
	inhead->altTD = 1;
	inhead->qhlp = ((uint32_t) inhead2) | 2;
	inhead->currentTD = 0;
	inhead->ch = 1 << 15; // T
	inhead->token = 0x40;

	is_periodic_transaction_is_running = 1;
	for (int i = 0; i < 1024; i++) {
		framelist[i] = ((uint32_t) inhead) | 2;
	}
}

static void ehci_probe_all_ports(int ports, struct ehci_operation* op) {
	serial_trace("EHCI Probing..\n");
	for (uint8_t i = 0; i < ports; i++) {
		ehci_port_reset(op, i);
		boolean_t available = (op->portsc[i] & EHCI_PORT_ENABLED);
		if (available) {
			uint8_t addr = i + 1;
			serial_trace("\nUSB Port %d Available\n", i);

			ehci_set_address(addr, op);

			struct usb_device_descriptor* desc =
				(struct usb_device_descriptor*)
					usb_get_descriptor(
						op, addr, 1, 0,
						sizeof(struct
						       usb_device_descriptor));
			serial_trace(" USB Port %d : Descriptor Length : %d\n",
				     i, desc->bLength);
			serial_trace(" USB Port %d : Descriptor Type : %d\n", i,
				     desc->bDescriptorType);
			serial_trace(" USB Port %d : Version : %x\n", i,
				     desc->bcdUSB);
			serial_trace(" USB Port %d : Device class : %d\n", i,
				     desc->bDeviceClass);
			serial_send_string("  USB Device sub class : ");
			serial_send_number(desc->bDeviceSubClass, 10);
			serial_send_string("\n");
			serial_send_string("USB Device protocol : ");
			serial_send_number(desc->bDeviceProtocol, 10);
			serial_send_string(" ");
			serial_send_string("USB Max packet size : ");
			serial_send_number(desc->bMaxPacketSize0, 10);
			serial_send_string(" ");
			serial_send_string("USB Number of Configuration : ");
			serial_send_number(desc->bNumConfigurations, 10);
			serial_send_string("\n vendor id : ");
			serial_send_number(desc->idVendor, 10);
			serial_send_string("\n");

			uint8_t devClass = desc->bDeviceClass;
			uint8_t devSubClass = desc->bDeviceSubClass;
			uint8_t devProtocol = desc->bDeviceProtocol;

			int retrc = 0;
			// retry_config:
			struct usb_config_descriptor* config =
				(struct usb_config_descriptor*)
					usb_get_descriptor(
						op, addr, 2, 0,
						sizeof(struct
						       usb_config_descriptor));

			// //usleep(1000);
			config = (struct usb_config_descriptor*)
				usb_get_descriptor(op, addr, 2, 0,
						   config->wTotalLength);

			// if (((config->bDescriptorType != 2) ||
			// (config->bLength == 0)) && retrc < 4) {
			//     retrc++;
			// usleep(10000);
			//     goto retry_config;
			// }

			serial_send_string("number interfaces available : ");
			serial_send_number(config->bNumInterfaces, 10);
			serial_send_string("\n index string configurtion : ");
			serial_send_number(config->iConfiguration, 10);
			serial_send_string("\n confiuration value : ");
			serial_send_number(config->bConfigurationValue, 10);
			serial_send_string("\n");
			serial_trace("configuration type : %d \n\n",
				     config->bDescriptorType);
			serial_trace("USB Port %d : max power : %dmA\n", i,
				     config->bMaxPower * 2);

			struct usb_interface* interface =
				(struct usb_interface*) ((uintptr_t) config
							 + config->bLength);
			serial_send_string("\nusb descriptor length : ");
			serial_send_number(interface->bLength, 10);
			serial_send_string("\n");
			serial_send_string("descriptor type: ");
			serial_send_number(interface->bDescriptorType, 10);
			serial_send_string("\n");
			serial_send_string("interface class : ");
			serial_send_number(interface->bInterfaceClass, 10);
			serial_send_string("  USB Device sub class : ");
			serial_send_number(interface->bInterfaceSubClass, 10);
			serial_send_string("\n Protocol : ");
			serial_send_number(interface->bInterfaceProtocol, 10);
			serial_send_string("\n number endpoint : ");
			serial_send_number(interface->bNumEndpoints, 10);
			serial_send_string("\n interface number : ");
			serial_send_number(interface->bInterfaceNumber, 10);
			serial_send_string("\n\n");

			if (devClass == 0 && devSubClass == 0) {
				devClass = interface->bInterfaceClass;
				devSubClass = interface->bInterfaceSubClass;
				devProtocol = interface->bInterfaceProtocol;
			}

			// get string descriptor
			if (config->iConfiguration > 0) {
				struct usb_string_descriptor* str =
					(struct usb_string_descriptor*) usb_get_descriptor(
						op, addr, 3,
						config->iConfiguration,
						config->bLength
							- (sizeof(struct
								  usb_config_descriptor)
							   + sizeof(
								   struct
								   usb_interface)
							   + interface->bNumEndpoints
								     * sizeof(
									     struct
									     usb_endpoint_descriptor)));
				serial_send_string("string descriptor : ");
				serial_send_number(str->bDescriptorType, 10);
				serial_send_string("\n");
			}

			struct usb_setup_packet* set_config =
				(struct usb_setup_packet*) ehci_alloc(
					sizeof(struct usb_setup_packet));
			// memset (set_config, 0, sizeof (struct
			// usb_setup_packet));
			set_config->bRequest = 9;
			set_config->bmRequestType = 0;
			set_config->wValue = 1;
			set_config->wIndex = 0;
			set_config->wLength = 0;
			serial_send_string("set configuration\n");
			ehci_send_packet_and_receive(op, addr, set_config, 0,
						     0);
			serial_send_string("set configuration done\n");

			struct usb_endpoint_descriptor* endpoint = 0;
			if (interface->bNumEndpoints > 0) {
				serial_trace("search endpoint\n");
				// looping until found endpoint descriptor
				uint32_t start = (uint32_t) config;
				while (start < (uint32_t) config
						       + config->wTotalLength) {
					struct usb_endpoint_descriptor* ep =
						(struct
						 usb_endpoint_descriptor*)
							start;
					if (ep->bDescriptorType == 0x05) {
						endpoint = ep;
						break;
					}
					start += ep->bLength;
				}

				serial_send_string("\nfound uSB Endpoint \nusb "
						   "descriptor length : ");
				serial_send_number(endpoint->bLength, 10);
				serial_send_string("\n");
				serial_send_string("descriptor type: ");
				serial_send_number(endpoint->bDescriptorType,
						   10);
				serial_send_string("\n");
				serial_send_string("endpoint number : ");
				serial_send_number(
					endpoint->bEndpointAddress & 0xF, 16);
				serial_trace(
					"\n endpoint type : %d\n",
					(endpoint->bEndpointAddress & (1 << 7))
						>> 7);
				serial_send_string("  bmAttributes : ");
				serial_send_number(endpoint->bmAttributes, 2);
				serial_send_string("  wMaxPacketSize : ");
				serial_send_number(endpoint->wMaxPacketSize,
						   10);
				serial_send_string("  bInterval : ");
				serial_send_number(endpoint->bInterval, 10);
				serial_send_string("\n");

				// set iddle
				struct usb_setup_packet* set_iddle =
					(struct usb_setup_packet*) ehci_alloc(
						sizeof(struct
						       usb_setup_packet));

				memset(set_iddle, 0,
				       sizeof(struct usb_setup_packet));
				set_iddle->bRequest = 0x0A;
				set_iddle->bmRequestType = 0b00100001;
				set_iddle->wValue =
					((uint16_t) endpoint->bInterval);
				set_iddle->wIndex = 0;
				set_iddle->wLength = 0;
				ehci_send_packet_and_receive(op, addr,
							     set_iddle, 0, 0);
				serial_send_string("setting iddle done\n\n");
			}

			struct usb_setup_packet* setup_packet =
				(struct usb_setup_packet*) ehci_alloc(
					sizeof(struct usb_setup_packet));
			setup_packet->bmRequestType = 0b10000001;
			setup_packet->bRequest = 0x06;
			setup_packet->wValue = 0x2200;
			setup_packet->wIndex = 0;
			setup_packet->wLength = 32;
			uint8_t* it = (uint8_t*) ehci_alloc(4096);

			serial_trace("EHCI: get report descriptor\n");

			int max_retry = 0;
		report_retry:
			ehci_send_packet_and_receive(op, addr, setup_packet, it,
						     0);
			// if (it[0] == 0 && max_retry < 5) {
			//     max_retry++;
			// usleep(10000);
			//     goto report_retry;
			// }

			serial_send_string("report : ");
			for (int i = 0; i < 32; i++) {
				serial_send_number(it[i], 16);
				serial_send_string(" ");
				KDEBUG(1, "%x ", it[i]);
			}
			KDEBUG(1, "\n");
			serial_send_string("\n\n");

			// / (ehci_qh_allocator, (void *)desc);

			if (devClass == 0x3) {
				serial_trace("HID Device\n");
				if (devProtocol == 1) {
					serial_trace("HID Device : Keyboard\n");
				} else if (devProtocol == 2) {
					serial_trace("HID Device : Mouse\n");
				}

				// send set protocol request
				struct usb_setup_packet* setup_packet =
					(struct usb_setup_packet*) ehci_alloc(
						sizeof(struct
						       usb_setup_packet));
				;

				setup_packet =
					(struct usb_setup_packet*) ALIGN_UP(
						(uintptr_t) setup_packet, 32);

				setup_packet->bmRequestType = 0b00100001;
				setup_packet->bRequest = 0x0B;
				setup_packet->wIndex =
					interface->bInterfaceNumber;
				setup_packet->wValue = 1;
				setup_packet->wLength = 0x0;
				ehci_send_packet_and_receive(
					op, addr, setup_packet, 0, 0);

				usb_read_in(op, addr, 256, 1, in_data);
			}
		}
	}
}

// Fungsi untuk memetakan scancode ke karakter (hanya contoh sederhana)
char scancode_to_char(uint8_t scancode, uint8_t modifiers) {
	static char lookup_table[128] = {
		0,   0,	  0,	0,   'a',  'b',	 'c', 'd',  'e',  'f', 'g', 'h',
		'i', 'j', 'k',	'l', 'm',  'n',	 'o', 'p',  'q',  'r', 's', 't',
		'u', 'v', 'w',	'x', 'y',  'z',	 '1', '2',  '3',  '4', '5', '6',
		'7', '8', '9',	'0', '\n', 0,	 0,   '\b', '\t', ' ', '-', '=',
		'[', ']', '\\', '#', ';',  '\'', '`', ',',  '.',  '/', 0,   0,
		0,   0,	  0,	0,   'A',  'B',	 'C', 'D',  'E',  'F', 'G', 'H',
		'I', 'J', 'K',	'L', 'M',  'N',	 'O', 'P',  'Q',  'R', 'S', 'T',
		'U', 'V', 'W',	'X', 'Y',  'Z',	 '!', '@',  '#',  '$', '%', '^',
		'&', '*', '(',	')', '_',  '+',	 '{', '}',  '|',  ':', '"', '~',
		'<', '>', '?',	0};

	if (scancode >= 128)
		return 0; // Invalid scancode

	if (modifiers)
		return lookup_table[scancode]
		       - 32; // Ubah ke huruf besar jika shift ditekan

	return lookup_table[scancode];
}

void ioforge_usb_ehci_interrupt() {
	// serial_trace("EHCI: interrupt\n");
	op->usbsts = 0x3f;
	op->usbintr = 1;

	if (is_trasaction_is_running) {
		is_trasaction_is_running = 0;
	}

	if (is_periodic_transaction_is_running) {
		has_new_data = 1;
		// serial_send_string("report : ");

		// serial_send_string("\n");
		memcopy((void*) vfs_data, (void*) in_data, 8);
		for (int i = 2; i < 8; i++) {
			char a[2] = {0};
			a[0] = scancode_to_char(in_data[i], in_data[0]);
			if (in_data[i] == 0)
				break;
			memcopy((void*) ((uintptr_t) vfs_data + 10), (void*) a,
				sizeof(char));
			// serial_trace("%s", a);
		}
		memset(in_data, 0, 32);
		// is_periodic_transaction_is_running = 0;
		usb_read_in(op, 1, 32, 1, in_data);
	}
}

uint8_t* ehci_block_read(void* this, uint64_t offset, size_t _count) {
	// no used in initrd
	// return (uint8_t *)((uintptr_t)(((block_device_operations_t
	// *)this)->ctx) + offset);
	return 0;
}

// int ehci_vfs_mount(vfs_inode_t* node) {
// 	serial_trace("initrd mount\n");

// 	uint64_t current_offset = node->offset;

// 	// TarHeader *header = (TarHeader
// 	// *)node->block->ops->read(node->block->ops, current_offset, 0);

// 	// while (strncmp(header->ustar, "ustar", 5) == 0) {
// 	//     int size = initrd_oct2bin(header->size, 11);
// 	//     // if (strncmp(header->filename, entry->name,
// 	//     sizeof(entry->name)) == 0) { serial_trace("ON MOUNT: found path
// 	//     %s\n", header->filename);

// 	//     // return 1;
// 	//     // }
// 	//     current_offset += (((+511) / 512) + 1) * 512;
// 	//     header = (TarHeader *)node->block->ops->read(node->block->ops,
// 	//     current_offset, 0);
// 	// }
// 	return 0;
// }

// vfs_operations_t* ehci_hid_vfs_impl() {
// 	vfs_operations_t* ops = (vfs_operations_t*)vxPhysBaseAlloc(
// 	    1 + sizeof(vfs_operations_t) / 4096);
// 	ops->open = ehci_vfs_open;
// 	// ops->mount = ehci_vfs_mount;
// 	return ops;
// }

boolean_t ehci_is_transaction_is_running() {
	return is_trasaction_is_running;
}
