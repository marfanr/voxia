#ifndef __VFS__RCU_H__
#define __VFS__RCU_H__

#include <type.h>

struct rcu_head {
	struct rcu_head* next; // rantai callback internal RCU
	void (*func)(
		struct rcu_head*); // fungsi yang dipanggil setelah grace period
};

void call_rcu(struct rcu_head* head, void (*func)(struct rcu_head*));

#endif // __VFS__RCU_H__