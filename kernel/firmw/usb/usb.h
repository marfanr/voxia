#ifndef __FIRMW__USB__USB_H__
#define __FIRMW__USB__USB_H__

#include <libk/type.h>

typedef struct {
  uint8_t deviceClass;
  uint8_t deviceSubClass;
  uint8_t deviceAddr;
  uint8_t deviceProtocol;
} usb_device;

#endif