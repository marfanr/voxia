#include <hal/cpu/paging.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <libk/str/memcopy.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <memory/virtual_memory_allocator.h>

#define PERCPU __attribute__((section(".data.percpu")))

extern uint64_t current_page_addr;

#define VMA_INITIAl_ZONE_COUNT 256

#define VMA_INTO_QUEUE_METADATA_ZONE(x) (uint64_t)(vma_zone_queue_metadata_start + x)
#define VMA_FROM_METADATA_ZONE(x) (uint64_t)(x - vma_zone_queue_metadata_start)
#define VMA_INTO_ZONE(x) (uint64_t)(vma_zone_start + x)
#define VMA_FROM_ZONE(x) (uint64_t)(x - vma_zone_start)
#define VMA_INTO_VMA_TREE(x) (uint64_t)(vma_tree_start + x)
#define VMA_FROM_VMA_TREE(x) (uint64_t)(x - vma_tree_start)

// global variable for zone
typedef struct vma_zone_chunk_list vma_zone_chunk_list;
struct vma_zone_chunk_list
{
    virtual_memory_zone_size   size;        // 8 bytes
    size_t                     count;       // 8 bytes
    virtual_memory_queue_list *queue_chunk; // 8 bytes
} __attribute__((aligned(64)));             // Align to L1 cache line (typically 64 bytes)

#define VM_ZONE_CHUNK_COUNT 8
static vma_zone_chunk_list vma_zone_chunk_lists[VM_ZONE_CHUNK_COUNT] = {
    (vma_zone_chunk_list){.size = VM_ZONE_8, .queue_chunk = 0},
    (vma_zone_chunk_list){.size = VM_ZONE_16, .queue_chunk = 0},
    (vma_zone_chunk_list){.size = VM_ZONE_64, .queue_chunk = 0},
    (vma_zone_chunk_list){.size = VM_ZONE_128, .queue_chunk = 0},
    (vma_zone_chunk_list){.size = VM_ZONE_256, .queue_chunk = 0},
    (vma_zone_chunk_list){.size = VM_ZONE_512, .queue_chunk = 0},
    (vma_zone_chunk_list){.size = VM_ZONE_1024, .queue_chunk = 0},
    (vma_zone_chunk_list){.size = VM_ZONE_4096, .queue_chunk = 0},
};

PERCPU static virtual_memory_zone vma_zone[VMA_INITIAl_ZONE_COUNT];
static int                        vma_zone_count = 0;

static virtual_memory_queue_list *current_virtual_memory_queue = 0;
static size_t                     current_vma_queue_size       = 0;

extern boolean_t paging_has_been_set;

// internal

static virtual_memory *virtual_memory_tree_chunk   = 0;
static size_t          allocated_memory_tree_count = 0;
static virtual_memory *virtual_memory_tree_root    = 0;
static size_t          virtual_memory_tree_count   = 0;
static uintptr_t       last_vma_zone_addr          = vma_zone_start;
static uintptr_t       last_vma_queue_addr         = vma_zone_queue_metadata_start;

static virtual_memory *
create_virtual_memory_tree()
{
    if (allocated_memory_tree_count < 1)
    {
        uintptr_t phys_addr         = (uintptr_t)phys_base_alloc(1);
        virtual_memory_tree_chunk   = (virtual_memory *)VMA_INTO_VMA_TREE((uintptr_t)phys_addr);
        allocated_memory_tree_count = BLOCK_SIZE / sizeof(virtual_memory);
        paging_mmap(paging_get_highest_page_map(), (uintptr_t)virtual_memory_tree_chunk,
                    (uintptr_t)phys_addr, 0b11);
    }
    virtual_memory *current = (virtual_memory *)virtual_memory_tree_chunk;
    virtual_memory_tree_chunk =
        (virtual_memory *)((uintptr_t)virtual_memory_tree_chunk + sizeof(virtual_memory));
    allocated_memory_tree_count--;
    return current;
}

static void
vma_tree_rotate_left(virtual_memory *node)
{
    virtual_memory *right_child = node->right_child;
    node->right_child           = right_child->left_child;

    if (right_child->left_child)
    {
        right_child->left_child->parent = node;
    }

    right_child->parent = node->parent;
    if (!node->parent)
    {
        virtual_memory_tree_root = right_child;
    }
    else if (node == node->parent->left_child)
    {
        node->parent->left_child = right_child;
    }
    else
    {
        node->parent->right_child = right_child;
    }

    right_child->left_child = node;
    node->parent            = right_child;
}
static void
vma_tree_rotate_right(virtual_memory *node)
{
    virtual_memory *left_child = node->left_child;
    node->left_child           = left_child->right_child;

    if (left_child->right_child)
    {
        left_child->right_child->parent = node;
    }

    left_child->parent = node->parent;
    if (!node->parent)
    {
        virtual_memory_tree_root = left_child;
    }
    else if (node == node->parent->right_child)
    {
        node->parent->right_child = left_child;
    }
    else
    {
        node->parent->left_child = left_child;
    }

    left_child->right_child = node;
    node->parent            = left_child;
}

static void
vma_tree_splay(virtual_memory *node)
{
    while (node->parent)
    {
        if (node->parent->parent == 0)
        {
            if (node == node->parent->left_child)
            {
                vma_tree_rotate_right(node->parent);
            }
            else
            {
                vma_tree_rotate_left(node->parent);
            }
        }
        else if (node == node->parent->left_child &&
                 node->parent == node->parent->parent->left_child)
        {
            vma_tree_rotate_right(node->parent->parent);
            vma_tree_rotate_right(node->parent);
        }
        else if (node == node->parent->right_child &&
                 node->parent == node->parent->parent->right_child)
        {
            vma_tree_rotate_left(node->parent->parent);
            vma_tree_rotate_left(node->parent);
        }
        else if (node == node->parent->right_child &&
                 node->parent == node->parent->parent->left_child)
        {
            vma_tree_rotate_left(node);
            vma_tree_rotate_right(node);
        }
        else
        {
            vma_tree_rotate_right(node);
            vma_tree_rotate_left(node);
        }
    }
}

void
vma_tree_add(uintptr_t virt_addr, uintptr_t phys_addr, size_t length)
{
    virtual_memory *new_tree = create_virtual_memory_tree();
    new_tree->virt_address   = virt_addr;
    new_tree->phys_address   = phys_addr;
    new_tree->length         = length;

    virtual_memory *current = virtual_memory_tree_root;
    virtual_memory *parent  = 0;
    while (current)
    {
        parent = current;
        if (current->virt_address < virt_addr)
        {
            current = current->right_child;
        }
        else
        {
            current = current->left_child;
        }
    }

    if (!parent)
    {
        virtual_memory_tree_root = new_tree;
    }
    else if (parent->virt_address < virt_addr)
    {
        parent->right_child = new_tree;
    }
    else
    {
        parent->left_child = new_tree;
    }

    vma_tree_splay(new_tree);
    virtual_memory_tree_count++;
}

virtual_memory *
vma_tree_find(uintptr_t virt_addr)
{
    virtual_memory *current = virtual_memory_tree_root;
    while (current)
    {
        if (current->virt_address == virt_addr)
        {
            vma_tree_splay(current);
            return current;
        }
        else if (current->virt_address < virt_addr)
        {
            current = current->right_child;
        }
        else
        {
            current = current->left_child;
        }
    }
    return 0;
}

void
vma_tree_debug()
{
    virtual_memory *current = virtual_memory_tree_root;
    while (current)
    {
        serial_trace("vma tree virt addr 0x%x phys addr 0x%x\n", current->virt_address,
                     current->phys_address);
        current = current->left_child;
    }
    current = virtual_memory_tree_root->right_child;
    while (current)
    {
        serial_trace("vma tree virt addr 0x%x phys addr 0x%x\n", current->virt_address,
                     current->phys_address);
        current = current->right_child;
    }
}

static void
refresh_allocation_queue()
{
    if (current_vma_queue_size < 1)
    {

        uintptr_t phys_addr          = (uintptr_t)phys_base_alloc(1);
        current_virtual_memory_queue = (virtual_memory_queue_list *)last_vma_queue_addr;
        last_vma_queue_addr += BLOCK_SIZE;

        current_vma_queue_size = BLOCK_SIZE / sizeof(virtual_memory_queue_list);

        paging_mmap(paging_get_highest_page_map(), (uintptr_t)current_virtual_memory_queue,
                    (uintptr_t)phys_addr, 0b11);
    }
}

virtual_memory_queue_list *
acquire_queue()
{
    refresh_allocation_queue();
    virtual_memory_queue_list *curr = (virtual_memory_queue_list *)current_virtual_memory_queue;
    current_virtual_memory_queue =
        (virtual_memory_queue_list *)((uintptr_t)current_virtual_memory_queue +
                                      sizeof(virtual_memory_queue_list));
    current_vma_queue_size--;

    return curr;
}

void
steal_phys_mem_to_chunk(virtual_memory_zone_size size, size_t phys_block_count)
{
    void *raw_chunk = (void *)(phys_base_alloc(phys_block_count));
    void *_chunk    = (void *)(last_vma_zone_addr);
    last_vma_zone_addr += 0x1000;

    /* paging_add_dma_mapping((uintptr_t)raw_chunk, (uintptr_t)_chunk, */
    /*                        phys_block_count); */

    size_t count = (phys_block_count * 0x1000) / size;

    virtual_memory_queue_list *queue = acquire_queue();
    queue->start                     = (uintptr_t)_chunk;

    vma_tree_add((uintptr_t)_chunk, (uintptr_t)raw_chunk, phys_block_count);
    virtual_memory_queue_list *current_queue = queue;

    for (size_t i = 1; i < count; i++)
    {
        virtual_memory_queue_list *new_queue = acquire_queue();

        new_queue->start    = (uintptr_t)_chunk + (i * size);
        current_queue->next = new_queue;
        current_queue       = new_queue;
    }

    for (size_t i = 0; i < VM_ZONE_CHUNK_COUNT; i++)
    {
        if (vma_zone_chunk_lists[i].size == size)
        {
            vma_zone_chunk_lists[i].count       = count;
            queue->next                         = vma_zone_chunk_lists[i].queue_chunk;
            vma_zone_chunk_lists[i].queue_chunk = queue;
            break;
        }
    }
}

static int
retrieve_queue_from_chunk(virtual_memory_zone_size size, virtual_memory_queue_list **queue)
{
    for (size_t i = 0; i < VM_ZONE_CHUNK_COUNT; i++)
    {
        if (vma_zone_chunk_lists[i].size == size)
        {
            virtual_memory_queue_list *current = vma_zone_chunk_lists[i].queue_chunk;
            vma_zone_chunk_lists[i].count--;
            vma_zone_chunk_lists[i].queue_chunk = vma_zone_chunk_lists[i].queue_chunk->next;
            *queue                              = current;
            return 0;
        }
    }
    return 1;
}

void
vma_create_new_zone(const char name[20], virtual_memory_zone_size size)
{
    for (size_t i = 0; i < VMA_INITIAl_ZONE_COUNT; i++)
    {
        if (!vma_zone[i].active)
        {
            memcopy((void *)vma_zone[i].name, (void *)name, 20);
            vma_zone[i].size   = size;
            vma_zone[i].active = 1;
            vma_zone_count++;
            break;
        }
    }
}

void
vma_setup_zone()
{
    steal_phys_mem_to_chunk(VM_ZONE_8, 1);
    steal_phys_mem_to_chunk(VM_ZONE_16, 4);
    steal_phys_mem_to_chunk(VM_ZONE_64, 8);
    steal_phys_mem_to_chunk(VM_ZONE_128, 2);
    steal_phys_mem_to_chunk(VM_ZONE_256, 8);

    vma_create_new_zone("vm.kernel", VM_ZONE_16);
    vma_create_new_zone("vm.permanent", VM_ZONE_16);
    vma_create_new_zone("vm.obj8", VM_ZONE_8);
    vma_create_new_zone("vm.obj64", VM_ZONE_64);
}

uint64_t
vma_request_zone(virtual_memory_zone_name name)
{
    for (int i = 0; i < vma_zone_count; i++)
    {
        if (strncmp(vma_zone[i].name, name, strlen(name)) == 0)
        {
            virtual_memory_queue_list *current = 0;

            if (retrieve_queue_from_chunk(vma_zone[i].size, &current))
            {
                serial_trace("error: vma zone chunk not enough \n");
                return 0;
            }

            current->next     = vma_zone[i].queue;
            vma_zone[i].queue = current;
            return current->start;
            break;
        }
    }

    return 0;
}
