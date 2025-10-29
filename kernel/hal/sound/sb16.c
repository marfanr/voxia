// #include "sb16.h"

// #include <firmw/pci/pci.h>
// #include <libk/io.h>
// #include <libk/serial.h>
// #include <libk/str.h>
// #include <libk/timer.h>
// #include <libk/type.h>
// #include <memory/memory_utils.h>
// #include <memory/phys_base_allocator.h>
// #include <vfs/vfs.h>

// uint16_t nam_base     = 0;
// uint16_t nabm_base    = 0;
// uint32_t extended_cap = 0;

// typedef struct
// {
//     uint32_t addr;
//     uint16_t size;
//     uint16_t flags;
// } bufer_list;

// bufer_list *buf;

// void
// set_sample_rate(uint32_t rate)
// {
//     if ((extended_cap & 0x1) == 0x1)
//     {
//         // set same variable rate on all outputs
//         outw(nam_base + 0x2c, rate);
//         outw(nam_base + 0x2e, rate);
//         outw(nam_base + 0x30, rate);
//         outw(nam_base + 0x32, rate);
//     }
// }

// void
// sb16_play(const char *name)
// {
//     int fd = vfs_open(name, 0);
//     if (fd < 0)
//     {
//         serial_trace("failed to open ff.pcm\n");
//         return;
//     }

//     struct vfs_file_stats stats;
//     vfs_fstat(fd, &stats);
//     uint8_t *buf_ = (uint8_t *)VIRT2PHYS(phys_base_alloc((1 + stats.size) / 4096));
//     vfs_read(fd, buf_, stats.size);

//     outb(nabm_base + 0x04, 0);

//     set_sample_rate(44100);

//     outb(nabm_base + 0x1b, 0x2);
//     int ticks = 0;
//     while ((inb(nabm_base + 0x1b) & 0x2) == 0x2)
//     {
//         asm("nop");
//         if (ticks > 50)
//         { // stream was not reseted after 100 ms
//             return;
//         }
//         ticks++;
//     }
//     outb(nabm_base + 0x1b, 0x0);

//     outl(nabm_base + 0x10, (uint32_t)buf);

//     // fill buffer
//     buf[0].addr = (uint32_t)buf_ + 0x1000;
//     buf[0].size = 0x1200;

//     for (int i = 1; i < 32; i++)
//     {
//         buf[i].addr = (uint32_t)buf_ + 0x2000 + (i * 0x2000);
//         buf[i].size = 0x2000;
//     }

//     // clear status
//     outw(nabm_base + 0x16, 0x1C);

//     // start streaming
//     outb(nabm_base + 0x1b, 0x1);
//     serial_trace("streaming started\n");
// }

// void
// sb16_init()
// {
//     // AC97
//     for (int i = 0; i < 32; i++)
//     {
//         if (pci_devices[i].class == 0x4 && pci_devices[i].subclass == 0x1)
//         {
//             serial_trace("\nFound Intel AC9 device BAR at: 0x%x and 0x%x\n",
//             pci_devices[i].bar[0],
//                          pci_devices[i].bar[1]);
//             enable_bus_mastering(pci_devices[i]);
//             enable_io_space(pci_devices[i]);

//             nam_base  = pci_devices[i].bar[0];
//             nabm_base = pci_devices[i].bar[1];
//             break;
//         }
//     }

//     // resume rom cold reet
//     outl(nabm_base + 0x2C, (0b00 << 22) | (0b00 << 20) | (0 << 2) | (1 << 1));
//     usleep(20);

//     outb(nabm_base + 0x0B, 0x2);
//     outb(nabm_base + 0x1B, 0x2);
//     outb(nabm_base + 0x2B, 0x2);

//     outw(nam_base + 0x00, 0xFF);

//     outw(nam_base + 0x18, 0x0);

//     buf = (bufer_list *)VIRT2PHYS(phys_base_alloc(1 + (sizeof(bufer_list) * 32) / 4096));

//     extended_cap = inw(nam_base + 0x28);
//     serial_trace("extended cap : 0x%x\n", extended_cap);
//     if ((extended_cap & 0x1) == 0x1)
//     {
//         outw(nam_base + 0x2A, 0x1);
//     }

//     // get number of AUX_OUT volume steps
//     uint32_t ac97_aux_out_number_of_volume_steps = 31;
//     outw(nam_base + 0x04, 0x2020);
//     if ((inw(nam_base + 0x04) & 0x2020) == 0x2020)
//     {
//         ac97_aux_out_number_of_volume_steps = 63;
//     }
//     serial_trace("AC97 AUX_OUT number of volume steps: %d\n",
//     ac97_aux_out_number_of_volume_steps);

//     serial_trace("AC997 driver initialized\n\n");
// }