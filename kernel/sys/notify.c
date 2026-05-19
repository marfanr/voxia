#include <spinlock.h>
#include "hash.h"
#include <notify.h>
#include <str.h>

static struct notify_dev_table notify_table = {0};

void notify_dev_create(kstring name) {
    auto h = hash(name->c_str, NOTIFY_DEV_HASH_SIZE);
    auto entry = notify_table.buckets[h & NOTIFY_DEV_HASH_MASK];

    auto n = (struct notify_dev*) kalloc(sizeof(struct notify_dev));
    n->hash = h;
    n->name = name;
    n->next = entry;

    spin_acquire(&notify_table.lock);
    notify_table.buckets[h & NOTIFY_DEV_HASH_MASK] = n;
    spin_release(&notify_table.lock);
}


int notify_register(char* name, struct notifier* n) {
    auto h = hash(name, NOTIFY_DEV_HASH_SIZE);
    auto current_dev = notify_table.buckets[h & NOTIFY_DEV_HASH_MASK];
    
    while (current_dev) {
        if (strncmp(current_dev->name->c_str, name, strlen(name)) == 0)
            break;
        current_dev = current_dev->next;
    }
    
    if (!current_dev)
        return 0;

    auto new_notifier = (struct notifier*) kalloc(sizeof(struct notifier));
    new_notifier->callback = n->callback;
    new_notifier->context = n->context;
    new_notifier->priority = n->priority;
    new_notifier->flags = n->flags;

    auto curr_chain = &current_dev->chain.head;
    while (*curr_chain && (*curr_chain)->priority >= new_notifier->priority)
        curr_chain = &(*curr_chain)->next;

    spin_acquire(&current_dev->chain.lock);
    new_notifier->next = *curr_chain;
    *curr_chain = new_notifier;
    __atomic_fetch_add(&current_dev->chain.size, 1, __ATOMIC_RELAXED);
    spin_release(&current_dev->chain.lock);
    
    return 1;
}

int notify_call(char* name, uint32_t event, void* data) {
    auto h = hash(name, NOTIFY_DEV_HASH_SIZE);
    auto current_dev = notify_table.buckets[h & NOTIFY_DEV_HASH_MASK];
    
    while (current_dev) {
        if (strncmp(current_dev->name->c_str, name, strlen(name)) == 0)
            break;
        current_dev = current_dev->next;
    }
    
    if (!current_dev)
        return 0;

    auto current_chain = current_dev->chain;
    while (current_chain.head) {
        current_chain.head->callback(event, data, current_chain.head->context);
        current_chain.head = current_chain.head->next;
    }
    
    return 1;

}