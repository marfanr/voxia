// #include "./kalloc.h"
// #include "./memory_utils.h"
// #include "./phys_base_allocator.h"
// #include <libk/serial.h>
// #include <libk/str.h>

// struct kalloc_region *kalloc_base_region;
// uint16_t current_region = 0;
// uint16_t actived_region = KALLOC_REGION_INIT;

// void
// kalloc_init ()
// {
//     kalloc_base_region = (struct kalloc_region *)VIRT2PHYS (phys_base_alloc
//     (
//         1 + (KALLOC_MAX_REGION * sizeof (struct kalloc_region)) /
//         BLOCK_SIZE));

//     // creating region
//     kalloc_region_add (KALLOC_REGION_INIT, VIRT2PHYS (phys_base_alloc
//     (9000)),
//                        BLOCK_SIZE * 9000); // 20Mb
//     kalloc_region_add (KALLOC_REGION_BLOCK, VIRT2PHYS (phys_base_alloc
//     (100)),
//                        100 * BLOCK_SIZE); // 400Kb for block and descriptor
//     kalloc_region_add (KALLOC_REGION_QH, VIRT2PHYS (phys_base_alloc (100)),
//                        100 * BLOCK_SIZE); // 400Kb for block and descriptor
// }

// void
// kalloc_switch_region (uint16_t id)
// {
//     for (uint16_t i = 0; i < current_region; i++)
//         {
//             if (kalloc_base_region[i].id == id)
//                 {
//                     actived_region = i;
//                     // serial_trace ("switch to region %d on index %d\n",
//                     id,
//                     // i);
//                     return;
//                 }
//         }
// }

// void
// kalloc_region_add (uint16_t id, uint64_t start, size_t size)
// {
//     kalloc_base_region[current_region].id = id;
//     kalloc_base_region[current_region].start = start;
//     kalloc_base_region[current_region].size = size;
//     kalloc_base_region[current_region].ussage = 0;

//     kalloc_base_region[current_region].block
//         = (struct buddy_allocator *)buddy_allocator_install ((void *)start,
//                                                              size);
//     current_region++;
// }

// static char *
// region_str (uint16_t region)
// {
//     switch (region)
//         {
//         case KALLOC_REGION_INIT:
//             return "KALLOC_REGION_INIT";
//         case KALLOC_REGION_BLOCK:
//             return "KALLOC_REGION_BLOCK";
//         case KALLOC_REGION_QH:
//             return "KALLOC_REGION_QH";
//         default:
//             return "UNKNOWN";
//         }
// }

// void
// kalloc_log ()
// {
//     size_t ussage = 0;
//     size_t size = 0;
//     for (uint64_t i = 0; i < current_region; i++)
//         {
//             ussage += kalloc_base_region[i].ussage;
//             size += kalloc_base_region[i].size;
//         }
//     serial_trace ("\nkalloc ussage : %d mb (%d bytes) / %d mb (%d bytes)\n",
//                   ussage / 1024 / 1024, ussage, size / 1024 / 1024, size);

//     // log per region
//     for (uint64_t i = 0; i < current_region; i++)
//         {
//             serial_trace ("\nregion %s : %d mb (%d Kb) / %d mb (%d Kb)\n",
//                           region_str (kalloc_base_region[i].id),
//                           kalloc_base_region[i].ussage / 1024 / 1024,
//                           kalloc_base_region[i].ussage / 1024,
//                           kalloc_base_region[i].size / 1024 / 1024,
//                           kalloc_base_region[i].size / 1024);
//             buddy_log (kalloc_base_region[i].block);
//         }
// }

// void *
// kalloc (size_t size)
// {
//     struct buddy_allocator *current_buddy
//         = kalloc_base_region[actived_region].block;
//     kalloc_base_region[actived_region].ussage
//         += buddy_find_visible_size (current_buddy, size);
//     return buddy_alloc (current_buddy, size);
// }

// void
// kfree (void *ptr)
// {
//     struct buddy_allocator *current_buddy
//         = kalloc_base_region[actived_region].block;
//     int ussage = buddy_free (current_buddy, ptr);
//     kalloc_base_region[actived_region].ussage -= ussage;
// }