#include "netdev.h"
#include "init/init.h"
#include "libk/hash.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "memory/slab.h"
#include <init/loader.h>

#define NETDEV_HASHMAP_MAX_ENTRY 512

static struct slab_cache* netdev_cache = 0;
struct netdev_list {
	netdev_t* buckets;
};

static struct netdev_list netdev_lists[NETDEV_HASHMAP_MAX_ENTRY];

static struct netdev_ops* ethernet_ops = 0;

static void bind_nic(netdev_t* netdev, struct ioforge_nic_service* nic);

INIT(Netdev) {
	vxCreateSlabCache(&netdev_cache, "netdev", sizeof(struct netdev), 0, 0);

	// init ethernet ops
	ethernet_ops = (struct netdev_ops*) kalloc(sizeof(struct netdev_ops));
	ethernet_ops->bind_nic = bind_nic;
}

int create_netdev(char* name, netdev_type_t type) {
	auto netdev = (struct netdev*) vxSlabAlloc(netdev_cache);
	memcopy(netdev->name, name, strlen(name));

	netdev->is_up = false;
	netdev->type = type;

	switch ((int) type) {
	case NETDEV_TYPE_ETHERNET:
		netdev->ops = ethernet_ops;
		break;

	default:
		// type tidak terdaftar
		return -1;
		break;
	}

	// general ops

	// put in hashmap
	auto hash_name = hash(name, NETDEV_HASHMAP_MAX_ENTRY);
	netdev->hash = hash_name;

	auto bucket = &netdev_lists[hash_name];

	if (!bucket->buckets) {
		bucket->buckets = netdev;
	} else {
		netdev->next = (void*) bucket->buckets;
		bucket->buckets = netdev;
	}

	netdev_lists[hash_name].buckets = netdev;
	return 0;
}

netdev_t* lookup_netdev(char* name) {
	auto hash_name = hash(name, NETDEV_HASHMAP_MAX_ENTRY);

	auto bucket = netdev_lists[hash_name];

	if (!bucket.buckets) {
		return 0;
	}

	auto curr = (netdev_t*) bucket.buckets;
	while (curr) {
		if (curr->hash == hash_name) {
			LOG2_DEBUG("NETDEV", "bucket at 0x%x", curr);
			return curr;
		}

		__builtin_prefetch(curr->next);
		curr = (netdev_t*) curr->next;
	}

	return 0;
}

// ops implementation
static void bind_nic(netdev_t* netdev, struct ioforge_nic_service* nic) {
	netdev->nic = nic;
	nic->ops->get_mac_address(netdev->mac);
	LOG2_INFO("netdev", "netdev mac %x", netdev->mac[0]);
}