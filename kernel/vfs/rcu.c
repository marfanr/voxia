#include <vfs/rcu.h>

void call_rcu(struct rcu_head* head, void (*func)(struct rcu_head*)) {
	head->func = func;
	// pthread_mutex_lock(&g_rcu_lock);
	// head->next = g_pending;
	// g_pending = head;
	// pthread_mutex_unlock(&g_rcu_lock);
}