#ifndef __LLIST_H__
#define __LLIST_H__

#include <type.h>

#ifdef __cplusplus
extern "C" {
#endif

struct llist_head {
	struct llist_head* next;
	struct llist_head* prev;
};

#define LLIST_INIT(n) = {&(n), &(n)}
#define LIST_HEAD(n) struct llist_head n = LLIST_INIT(n)

static inline void llist_init(struct llist_head* head) {
	head->next = head;
	head->prev = head;
}

static inline void llist_add(struct llist_head* new_, struct llist_head* next,
			     struct llist_head* prev) {
	new_->next = next;
	new_->prev = prev;
	next->prev = new_;
	prev->next = new_;
}

static inline void
llist_add_tail(struct llist_head* new_, struct llist_head* head) {
	llist_add(new_, head, head->prev);
}

static inline void
llist_del_init(struct llist_head* prev, struct llist_head* next) {
	next->prev = prev;
	prev->next = next;
}

static inline void llist_del(struct llist_head* entry) {
	llist_del_init(entry->prev, entry->next);
	entry->next = NULL;
	entry->prev = NULL;
}

#define list_entry(ptr, type, member) container_of(ptr, type, member)

#define list_for_each_entry(pos, head, member)                                 \
	for (pos = list_entry((head)->next, typeof(*pos), member);             \
	     &pos->member != (head);                                           \
	     pos = list_entry(pos->member.next, typeof(*pos), member))

#ifdef __cplusplus
}
#endif

#endif // __LLIST_H__