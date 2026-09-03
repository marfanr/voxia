#include <vfs/rcu.h>
#include <procc/workqueue.h>

void call_rcu(struct rcu_head* head, void (*func)(struct rcu_head*)) {
	head->func = func;
	if (func) {
		vxAddWorkqueueTask((void (*)(void*))func, head, 0);
	}
}