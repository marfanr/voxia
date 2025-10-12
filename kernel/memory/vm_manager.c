#include "config.h"
#include "libk/type.h"
#include <hal/cpu/paging.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/slab.h>
#include <memory/vm_manager.h>

#define RBT_TYPE virtual_memory
#define RBT_ID_NAME start_address
#include <libk/tree/rbt.h>

#define PERCPU __attribute__((section(".data.percpu")))

// extern variables
extern boolean_t paging_has_been_set;

static rbt_node *VMA_RBT_NIL              = 0;
static rbt_node *virtual_memory_tree_root = 0;

struct slab_cache *rbt_node_cache = NULL;
struct slab_cache *vma_cache      = NULL;

void
vma_setup_zone()
{
    slab_cache_create(&rbt_node_cache, "rbt_node", sizeof(rbt_node), 64, 0);
    slab_cache_create(&vma_cache, "vma", sizeof(virtual_memory), 64, 0);

    VMA_RBT_NIL                      = (rbt_node *)slab_alloc(rbt_node_cache);
    VMA_RBT_NIL->data                = (virtual_memory *)slab_alloc(vma_cache);
    VMA_RBT_NIL->data->start_address = 0;
    VMA_RBT_NIL->data->end_address   = 0;
    VMA_RBT_NIL->left = VMA_RBT_NIL->right = VMA_RBT_NIL->parent = VMA_RBT_NIL;
    virtual_memory_tree_root                                     = VMA_RBT_NIL;
}

void
vma_register(uintptr_t phys_address, uintptr_t virt_addr, size_t size)
{
    virtual_memory *node = (virtual_memory *)slab_alloc(vma_cache);
    node->used           = true;
    node->start_address  = virt_addr;
    node->end_address    = virt_addr + size;
    node->phys_address   = phys_address;
    node->length         = size;
    node->flags          = 0;
    node->core           = 0;

    rbt_node *n = (rbt_node *)slab_alloc(rbt_node_cache);
    rbt_insert_node(&virtual_memory_tree_root, n, node, VMA_RBT_NIL);
}

virtual_memory *
vma_find(uintptr_t virt_addr)
{
    struct rbt_node *n = rbt_search_node(virtual_memory_tree_root, virt_addr, VMA_RBT_NIL);
    return n->data;
}

void
vma_unregister(uintptr_t virt_addr)
{
    struct rbt_node *n = rbt_search_node(virtual_memory_tree_root, virt_addr, VMA_RBT_NIL);
    if (n == VMA_RBT_NIL)
        return;
    rbt_remove_node(&virtual_memory_tree_root, n, VMA_RBT_NIL);
}

#undef RBT_ID_NAME
#undef RBT_TYPE
