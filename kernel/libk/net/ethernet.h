#ifndef __NET__ETHERNET_H__
#define __NET__ETHERNET_H__

#include <libk/type.h>

#define ETHERNET_TYPE_IPV4 0x0800
#define ETHERNET_TYPE_ARP 0x0806

typedef struct {
  uint8_t dest[6];
  uint8_t src[6];
  uint16_t type;
} __attribute__((packed)) ethernet_header_t;

#endif // __NET__ETHERNET_H__