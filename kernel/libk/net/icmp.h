#ifndef __NET__ICMP_H__
#define __NET__ICMP_H__

typedef struct {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint32_t rest;
} __attribute__((packed)) icmp_header_t;

#endif // __NET__ICMP_H__