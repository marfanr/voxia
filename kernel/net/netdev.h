#ifndef __NET__NETDEV_H__
#define __NET__NETDEV_H__

#include "libk/type.h"
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
	int (*open)(netdev_t* dev);
	int (*stop)(netdev_t* dev);
	int (*xmit)(netdev_t* dev, struct netbuff* nb);
	int (*set_mac_address)(netdev_t* dev, void* addr);
};

struct netdev {
	boolean_t is_up;
	netdev_type_t type;
	char name[NETDEV_NAME_MAX_LEN];
	uint8_t mac[NIC_MAC_LEN];

	struct ioforge_nic_service* nic;
};

struct netdev* create_netdev(char* name, uint8_t mac[NIC_MAC_LEN]);

#endif // __NET__NETDEV_H__