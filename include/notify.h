#ifndef __DEV__NOTIFY_H__
#define __DEV__NOTIFY_H__

#include "spinlock.h"
#include "string.h"
#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Notifier callback return codes */
#define NOTIFY_OK 1
#define NOTIFY_STOP -1
#define NOTIFY_BAD -2

/* Notifier flags */
#define NOTIFIER_FLAG_ONCE (1 << 0)
#define NOTIFIER_FLAG_ATOMIC (1 << 1)
#define NOTIFIER_FLAG_DISABLED (1 << 2)

#define NOTIFY_HIGHT 1000
#define NOTIFY_MEDIUM 500
#define NOTIFY_NORMAL 100
#define NOTIFY_LOW 50

typedef void (*notify_callback_t)(uint32_t event, void* data, void* context);

struct notifier {
	notify_callback_t callback;
	void* context;
	uint32_t priority;
	uint32_t flags;
	struct notifier* next;
};

struct notify_chain {
	spinlock_t lock;
	size_t size;
	struct notifier* head;
};

#define NOTIFY_DEV_HASH_SIZE 64
#define NOTIFY_DEV_HASH_MASK (NOTIFY_DEV_HASH_SIZE - 1)


struct notify_dev_table {
	spinlock_t lock;
	struct notify_dev* buckets[NOTIFY_DEV_HASH_SIZE];
};

struct notify_dev {
	uint64_t hash;
	kstring name;
	atomic_t refcount;
	struct notify_chain chain;
	struct notify_dev* next;
	volatile uint64_t event_received;
};

/* Lifecycle */
void notify_dev_create(kstring name);
int notify_dev_destroy(kstring name);

/* Subscription */
int notify_register(char* name, struct notifier* n);
int notify_unregister(kstring name, struct notifier* n);

int wait_until_receive_notify(const char* name, uint64_t timeout);

/* Dispatch */
int notify_call(char* name, uint32_t event, void* data);

#ifdef __cplusplus
}
#endif

#endif /* __DEV__NOTIFY_H__ */