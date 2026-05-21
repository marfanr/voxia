#include "hal/acpi/hpet.h"
#include "hal/timer/timer.h"
#include "hash.h"
#include "libk/serial.h"
#include <notify.h>
#include <spinlock.h>
#include <str.h>

static struct notify_dev_table notify_table = {0};

/* Internal helper: find a notify_dev by name.
 * Acquires and releases notify_table.lock internally. */
static struct notify_dev* find_dev(const char* name) {
	auto h = hash(name, NOTIFY_DEV_HASH_SIZE);
	size_t name_len = strlen(name);

	spin_acquire(&notify_table.lock);
	auto dev = notify_table.buckets[h & NOTIFY_DEV_HASH_MASK];
	while (dev) {
		if (strlen(dev->name->c_str) == name_len &&
		    strncmp(dev->name->c_str, name, name_len) == 0)
			break;
		dev = dev->next;
	}
	spin_release(&notify_table.lock);

	return dev;
}

void notify_dev_create(kstring name) {
	auto h = hash(name->c_str, NOTIFY_DEV_HASH_SIZE);

	auto n = (struct notify_dev*)kalloc(sizeof(struct notify_dev));
	n->hash = h;
	n->name = name;

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
	new_notifier->callback = n->callback;
	new_notifier->context = n->context;
	new_notifier->priority = n->priority;
	new_notifier->flags = n->flags;

	spin_acquire(&current_dev->chain.lock);

	auto curr_chain = &current_dev->chain.head;
	while (*curr_chain && (*curr_chain)->priority >= new_notifier->priority)
		curr_chain = &(*curr_chain)->next;

	new_notifier->next = *curr_chain;
	*curr_chain = new_notifier;
	__atomic_fetch_add(&current_dev->chain.size, 1, __ATOMIC_RELAXED);

	spin_release(&current_dev->chain.lock);

	return 1;
}

int notify_call(char* name, uint32_t event, void* data) {
	auto current_dev = find_dev(name);
	if (!current_dev)
		return 0;

	spin_acquire(&current_dev->chain.lock);

	auto current = current_dev->chain.head;
	while (current) {
		auto next = current->next;
		current->callback(event, data, current->context);
		current = next;
	}

	spin_release(&current_dev->chain.lock);

	__atomic_store_n(&current_dev->event_received, 1, __ATOMIC_RELEASE);

	return 1;
}

/* timeout in ms */
int wait_until_receive_notify(char* name, uint64_t timeout) {
	auto current_dev = find_dev(name);
	if (!current_dev)
		return 0;

	uint64_t initial =
	    __atomic_load_n(&current_dev->event_received, __ATOMIC_ACQUIRE);

	// (void)timeout;
	uint64_t timeout_count = 0;
	while (timeout_count < timeout) {
		if (__atomic_load_n(&current_dev->event_received,
		                    __ATOMIC_ACQUIRE) != initial)
			return 1;

		usleep(ms2ns(1));
		timeout_count += 1;
	}

	return 0;
}