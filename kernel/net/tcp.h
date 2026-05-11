#ifndef __NET__TCP_H__
#define __NET__TCP_H__

#include "net/ipv4.h"
#include "net/netdev.h"
#include <type.h>

struct __attribute__((packed)) tcp_header {
	uint16_t source_port;
	uint16_t destination_port;
	uint32_t sequence;
	uint32_t acknowledgment;

	uint8_t offset;
	uint8_t flags;

	uint16_t window;
	uint16_t checksum;
	uint16_t urgent_ptr;
} __attribute__((packed));

void handle_tcp(netdev_t* dev, struct ipv4_header* ip, uint8_t mac_dst[6]);

typedef struct {
	uint16_t mss; // 0 = tidak ada
	uint8_t has_wscale;
	uint8_t wscale; // 0 = tidak ada
	uint8_t sack_permitted;
	uint8_t has_timestamp;
	uint32_t ts_val;
	uint32_t ts_ecr;
} tcp_options_t;

void send_tcp_data(netdev_t* dev, struct ipv4_header* ip,
		   struct tcp_header* tcp, uint8_t* data, size_t len,
		   uint8_t mac_dst[6]);

void parse_tcp_options(struct tcp_header* tcp, tcp_options_t* out);
uint8_t
build_synack_options(netdev_t* dev, uint8_t* buf, tcp_options_t* client_opts);

void send_command(netdev_t* dev, struct ipv4_header* ip, struct tcp_header* tcp,
		  tcp_options_t* opt, uint8_t flags, uint8_t mac_dst[6]);

#endif // __NET__TCP_H__