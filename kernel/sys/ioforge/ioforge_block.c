
#include "ioforge/ioforge_block.h"
#include "ioforge/ioforge.h"
#include <type.h>

static bool ioforge_can_contain_block_device(IoForgeType type) {
	switch (type) {
	case IOFORGE_ROOT:
	case IOFORGE_BLOCK:
		return true;
	default:
		return false;
	}
}

void KERNEL_API foreach_block_device_by_type(
	struct ioforge_device* node, uint8_t type,
	ioforge_block_visitor_fn callback, void* ctx) {

	if (!node)
		return;

	if (!ioforge_can_contain_block_device(node->type))
		return;

	struct ioforge_block_device* block =
		(struct ioforge_block_device*) node;
	if (block->type == type) {
		callback(block, ctx);
	}

	struct ioforge_device* child = node->first_child;
	while (child) {
		foreach_block_device_by_type(child, type, callback, ctx);
		child = child->next_sibling;
	}
}