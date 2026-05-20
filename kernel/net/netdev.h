#ifndef __NET__NETDEV_H__
#define __NET__NETDEV_H__

#include <type.h>
#include "net/netbuff.h"
#define NETDEV_NAME_MAX_LEN 64

#include <type.h>
#include <ioforge/ioforge_nic.h>

typedef enum {
	NETDEV_TYPE_ETHERNET = 0, // Hardware fisik (E1000, RTL8139, dll)
	NETDEV_TYPE_LOOPBACK,	  // Virtual localhost (127.0.0.1)
	NETDEV_TYPE_BRIDGE,	  // Virtual switch
	NETDEV_TYPE_TUN,	  // Layer 3 Tunnel
	NETDEV_TYPE_TAP		  // Layer 2 Tunnel
} netdev_type_t;

typedef struct netdev netdev_t;
struct netdev_ops {
	int (*init)(struct netdev* dev);
	int (*open)(struct netdev* dev);
	int (*stop)(struct netdev* dev);
	int (*transmit)(struct netdev* dev, uint8_t* packet, uint16_t len);
	int (*set_mac)(struct netdev* dev, const uint8_t* new_mac);
	void (*bind_nic)(struct netdev* dev, struct ioforge_nic_service* nic);
	void (*unbind_nic)(struct netdev* dev);
};

struct netdev {
	uint64_t hash;
	boolean_t is_up;
	netdev_type_t type;
	char name[NETDEV_NAME_MAX_LEN];
	uint8_t mac[NIC_MAC_LEN];
	uint16_t mtu;

	struct ioforge_nic_service* nic;
	struct netdev_ops* ops;

	uint16_t ip_id_counter;
	// for hash map colision handling
	void* next;
};

int create_netdev(char* name, netdev_type_t type);
netdev_t* lookup_netdev(char* name);
uint16_t get_next_ip_id(netdev_t* dev);
#endif // __NET__NETDEV_H__