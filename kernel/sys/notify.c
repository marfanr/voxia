#include "hal/acpi/hpet.h"
#include "hal/timer/timer.h"
#include "hash.h"
#include "libk/serial.h"
#include <notify.h>
#include <spinlock.h>
#include <str.h>

static struct notify_dev_table notify_table = {0};

static struct notify_dev* find_dev(const char* name) {
	auto h = hash(name, NOTIFY_DEV_HASH_SIZE);
	size_t name_len = strlen(name);

	spin_acquire(&notify_table.lock);
	auto dev = notify_table.buckets[h & NOTIFY_DEV_HASH_MASK];
	while (dev) {
		if (strlen(dev->name->c_str) == name_len &&
		    strncmp(dev->name->c_str, name, name_len) == 0) {
			__atomic_fetch_add(&dev->refcount.counter, 1,
			                   __ATOMIC_RELAXED);
			break;
		}
		dev = dev->next;
	}
	spin_release(&notify_table.lock);

	return dev;
}

static void notify_dev_put(struct notify_dev* dev) {
	if (!dev)
		return;
	if (__atomic_sub_fetch(&dev->refcount.counter, 1, __ATOMIC_RELEASE) ==
	    0) {
		kfree2(dev);
	}
}

void notify_dev_create(kstring name) {
	auto h = hash(name->c_str, NOTIFY_DEV_HASH_SIZE);

	auto n = (struct notify_dev*)kalloc(sizeof(struct notify_dev));
	if (!n)
		return;

	memset(n, 0, sizeof(struct notify_dev));
	n->hash = h;
	n->name = name;
	n->chain.lock = (spinlock_t)SPINLOCK_INIT;
	__atomic_store_n(&n->refcount.counter, 1, __ATOMIC_RELAXED);

	spin_acquire(&notify_table.lock);

	auto existing = notify_table.buckets[h & NOTIFY_DEV_HASH_MASK];
	size_t name_len = strlen(name->c_str);
	while (existing) {
		if (strlen(existing->name->c_str) == name_len &&
		    strncmp(existing->name->c_str, name->c_str, name_len) ==
		        0) {
			spin_release(&notify_table.lock);
			kfree2(n);
			return;
		}
		existing = existing->next;
	}

	n->next = notify_table.buckets[h & NOTIFY_DEV_HASH_MASK];
	notify_table.buckets[h & NOTIFY_DEV_HASH_MASK] = n;

	spin_release(&notify_table.lock);
}

int notify_register(char* name, struct notifier* n) {
	auto current_dev = find_dev(name);
	if (!current_dev)
		return 0;

	auto new_notifier = (struct notifier*)kalloc(sizeof(struct notifier));
	if (!new_notifier)
		return 0;

	new_notifier->callback = n->callback;
	new_notifier->context = n->context;
	new_notifier->priority = n->priority;
	new_notifier->flags = n->flags;

	spin_acquire(&current_dev->chain.lock);

	auto curr_chain = &current_dev->chain.head;
	while (*curr_chain && (*curr_chain)->priority >= new_notifier->priority)
		curr_chain = &(*curr_chain)->next;

	serial2_printf("notift kntl\n");

	new_notifier->next = *curr_chain;
	*curr_chain = new_notifier;
	__atomic_fetch_add(&current_dev->chain.size, 1, __ATOMIC_RELAXED);

	spin_release(&current_dev->chain.lock);

	notify_dev_put(current_dev);
	return 1;
}

int notify_call(char* name, uint32_t event, void* data) {
	auto current_dev = find_dev(name);
	if (!current_dev)
		return 0;

#define MAX_SNAPSHOT 64
	struct notifier* snapshot[MAX_SNAPSHOT];
	int count = 0;

	spin_acquire(&current_dev->chain.lock);
	auto current = current_dev->chain.head;
	while (current && count < MAX_SNAPSHOT) {
		snapshot[count++] = current;
		current = current->next;
	}

	spin_release(&current_dev->chain.lock);

	for (int i = 0; i < count; i++) {
		if (snapshot[i]->callback)
			snapshot[i]->callback(event, data,
			                      snapshot[i]->context);
	}
	__atomic_fetch_add(&current_dev->event_received, 1, __ATOMIC_RELEASE);

	notify_dev_put(current_dev);
	return 1;
}

/* timeout in ms */
int wait_until_receive_notify(const char *name, uint64_t timeout) {
	struct notify_dev *current_dev = find_dev(name);
	if (!current_dev)
		return 0;
 
	uint64_t initial =
	    __atomic_load_n(&current_dev->event_received, __ATOMIC_ACQUIRE);
 
	uint64_t timeout_count = 0;
	while (timeout_count < timeout) {
		uint64_t current_gen = __atomic_load_n(
		    &current_dev->event_received, __ATOMIC_ACQUIRE);
		if (current_gen != initial) {
			notify_dev_put(current_dev);
			return 1;
		}
 
		usleep(ms2ns(1));
		timeout_count += 1;
	}
 
	notify_dev_put(current_dev);
	return 0;
}