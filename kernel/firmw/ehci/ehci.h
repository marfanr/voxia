#ifndef __FIRMW__EHCI__EHCI_H__
#define __FIRMW__EHCI__EHCI_H__

#include <libk/type.h>

#define __PACKED __attribute__((packed))

typedef struct {
    uint32_t          qhlp;
    uint32_t          ch;
    uint32_t          cap;
    volatile uint32_t currentTD;

    volatile uint32_t nextTD;
    volatile uint32_t altTD;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t extbuffer[5];

    uint32_t  nextqh;
    boolean_t used;
    boolean_t done;
    uint8_t   pad[20];

} __PACKED EhciQH;

typedef struct {
    volatile uint32_t link;
    volatile uint32_t altlink;
    volatile uint32_t token;
    volatile uint32_t buffer[5];
    volatile uint32_t extbuffer[5];

    boolean_t used;
    uint32_t  next;
} __PACKED EhciQTD;

#define TD_BYTE_SHIFT 16
#define TD_PID_SHIFT 8
#define TD_CER_SHIFT 10
#define TD_TERMINATE 1
#define TD_ACTIVE 1 << 7

typedef struct {
    volatile uint32_t usbcmd;
    volatile uint32_t usbsts;
    volatile uint32_t usbintr;
    volatile uint32_t frindex;
    volatile uint32_t ctrldssegment;
    volatile uint32_t periodiclistbase;
    volatile uint32_t asynclistaddr;
    volatile uint32_t reserved[9];
    volatile uint32_t configflag;
    volatile uint32_t portsc[];
} __attribute__((packed)) ehci_operation_t;

#define CMD_START 1
#define CMD_RESET 1 << 1
#define CMD_INTERUPT_THRESOLD_CTRL(x) x << 16

#define CONFIGFLAG_CF 1

#define PORT_ENABLED 1 << 2
#define PORT_RESET 1 << 8
#define PORT_SUSPEND 1 << 7
#define PORT_CURR_CONNECT_STATUS 1
#define PORT_POWER 1 << 12

// QTD PID
#define QH_HLNK_TERMINATE 0x00000001
#define PID_SETUP 0b10
#define PID_IN 0b01
#define PID_OUT 0b00
#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_DESC_DEVICE 0x01

#define ENABLE_INT_ON_COMPLETE 1 << 15

#define USB_REQ_GET_DESCRIPTOR 0x06
#define USB_DESC_DEVICE 0x0

#define QH_HRLF 1 << 15 // Head of Reclamation List Flag
enum {
    ITD_PTR  = 0 << 1,
    QH_PTR   = 1 << 1,
    SITD_PTR = 2 << 1,
    FSTN     = 3 << 0,
};

typedef struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

typedef struct usb_interface {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed)) usb_interface_t;

typedef struct usb_config_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

typedef struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

typedef struct usb_string_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wData[];
} __attribute__((packed)) usb_string_descriptor_t;

typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_packet_t;

#define MAX_QUE_HEAD 16
#define MAX_QUE_TD 64

#define DATA_TOGGLE 1 << 31
#define EHCI_ASYNC_ENABLE 1 << 5

#define USB_PACKET_OUT 0
#define USB_PACKET_IN 1
#define USB_PACKET_SETUP 2

#define EHCI_SUBCLASS 0x03
#define EHCI_CLASS 0x0C

#define PORT_RESET 1 << 8

void setup_ehci();
void ehci_send_packet_and_receive(uint8_t addr, usb_setup_packet_t *packet,
                                  void *data, uint8_t interrupt);
void ehci_send_packet_interupt(uint8_t addr, uint8_t length, void *data,
                               uint8_t interrupt);
void ehci_interrupt();

typedef struct itd {
    volatile uint32_t next;
    volatile uint32_t transaction[8];
    volatile uint32_t buffer[7];
} __attribute__((packed)) itd_t;

#endif // __FIRMW__EHCI__EHCI_H__
