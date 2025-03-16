#include "./pci.h"
#include <libk/io.h>
#include <libk/serial.h>
#include <libk/str.h>
#include <memory/buddy_allocator.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>
#include <sys/ioforge/ioforge.h>
#include <sys/ioforge/ioforge_pci.h>

#include <hal/usb/ehci.h>

#define PCI_COMMAND 0XCF8
#define PCI_DATA 0xCFC

static struct buddy_allocator *pci_allocator;

static uint16_t
pci_readw (uint8_t bus, uint8_t device, uint8_t func, uint8_t offset)
{
    uint32_t address;
    uint16_t tmp = 0;
    address = (uint32_t)(((uint32_t)bus << 16) | ((uint32_t)device << 11)
                         | ((uint32_t)func << 8) | (offset & 0xFC)
                         | ((uint32_t)0x80000000));

    outl (PCI_COMMAND, address);
    tmp = (uint16_t)((inl (PCI_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
    return tmp;
}

static void
pci_writew (uint8_t bus, uint8_t device, uint8_t func, uint8_t offset,
            uint32_t value)
{
    uint32_t address;
    address = (uint32_t)(((uint32_t)bus << 16) | ((uint32_t)device << 11)
                         | ((uint32_t)func << 8) | (offset & 0xFC)
                         | ((uint32_t)0x80000000));
    outl (PCI_COMMAND, address);
    outl (PCI_DATA, value);
}

static void pci_check_bus (size_t bus);

static void
pci_check_func (size_t bus, size_t device, size_t func)
{
    uint8_t class = pci_readw (bus, device, func, 0x0A) >> 8;
    uint8_t subclass = pci_readw (bus, device, func, 0x0A) & 0xFF;

    if (class == 0x06 && subclass == 0x04)
        {
            serial_trace ("PCI to PCI bridge found\n");
            uint8_t secondary_bus = pci_readw (bus, device, func, 0x19);
            pci_check_bus (secondary_bus);
        }
}

static void
pci_gather_info (size_t bus, size_t device, size_t func)
{
    struct IoForgePCI *pci = (struct IoForgePCI *)buddy_alloc (
        pci_allocator, sizeof (struct IoForgePCI));
    memset (pci, 0, sizeof (struct IoForgePCI));
    serial_trace ("\npci %d at 0x%x\n\n", bus, pci);

    pci->service.type = IOFORGE_PCI;
    pci->device_id = pciConfigReadWord (bus, device, func, 2);
    pci->command = pciConfigReadWord (bus, device, func, 4);
    pci->status = pciConfigReadWord (bus, device, func, 6);
    pci->revision_id = pciConfigReadWord (bus, device, func, 8) & 0xFF;
    pci->prog_if = (pciConfigReadWord (bus, device, func, 8) >> 8) & 0xFF;
    pci->subclass = pciConfigReadWord (bus, device, func, 10) & 0xFF;
    pci->class = (pciConfigReadWord (bus, device, func, 10) >> 8) & 0xFF;

    uint8_t cache_line_size = pciConfigReadWord (bus, device, func, 12) & 0xFF;
    uint8_t latency_timer = pciConfigReadWord (bus, device, func, 13) & 0xFF;
    uint8_t header_type = pciConfigReadWord (bus, device, func, 14) & 0xFF;

    uint8_t bist = pciConfigReadWord (bus, device, func, 15) & 0xFF;
    for (int i = 0; i < 6; i++)
        {
            uint32_t bar
                = pciConfigReadWord (bus, device, func, 16 + i * 4) & 0xFFFF;
            bar |= pciConfigReadWord (bus, device, func, 16 + i * 4 + 2) << 16;
            pci->bar[i].address = bar;
            pci->bar[i].iospace = bar & 1;
        }
    uint32_t cardbus_cis_pointer
        = pciConfigReadWord (bus, device, func, 41) & 0xFFFF;
    cardbus_cis_pointer |= pciConfigReadWord (bus, device, func, 43) << 16;
    uint16_t subsystem_vendor_id = pciConfigReadWord (bus, device, func, 44);
    uint16_t subsystem_id = pciConfigReadWord (bus, device, func, 46);
    uint32_t expansion_rom_base_address
        = pciConfigReadWord (bus, device, func, 48) & 0xFFFF;
    expansion_rom_base_address |= pciConfigReadWord (bus, device, func, 50)
                                  << 16;
    uint8_t capabilities_pointer
        = pciConfigReadWord (bus, device, func, 52) & 0xFF;
    uint8_t interrupt_line = pciConfigReadWord (bus, device, func, 60) & 0xFF;
    uint8_t interrupt_pin = pciConfigReadWord (bus, device, func, 60) >> 8;
    uint8_t min_grant = pciConfigReadWord (bus, device, func, 62) & 0xFF;
    uint8_t max_latency = pciConfigReadWord (bus, device, func, 63) & 0xFF;

    serial_trace ("class : %x subclass : %x \n", pci->class, pci->subclass);

    // turnon bus mastering
    pci_writew (bus, device, func, 4,
                pci->command | 0x4 | (pci->status << 16));

    // initialize standard supported device
    switch (pci->class)
        {
        case 0x00:
            switch (pci->subclass)
                {
                case 0x00:
                    serial_trace ("Unclassified device\n");
                    break;
                case 0x01:
                    serial_trace ("Mass storage controller\n");
                    break;
                case 0x02:
                    serial_trace ("Network controller\n");
                    break;
                case 0x03:
                    serial_trace ("Display controller\n");
                    break;
                case 0x04:
                    serial_trace ("Multimedia controller\n");
                    break;
                case 0x05:
                    serial_trace ("Memory controller\n");
                    break;
                case 0x06:
                    serial_trace ("Bridge device\n");
                    break;
                case 0x07:
                    serial_trace ("Simple communication controller\n");
                    break;
                case 0x08:
                    serial_trace ("Base system peripheral\n");
                    break;
                case 0x09:
                    serial_trace ("Input device controller\n");
                    break;
                case 0x0A:
                    serial_trace ("Docking station\n");
                    break;
                case 0x0B:
                    serial_trace ("Processor\n");
                    break;
                case 0x0C:
                    serial_trace ("Serial bus controller\n");
                    break;
                case 0x0D:
                    serial_trace ("Wireless controller\n");
                    break;
                case 0x0E:
                    serial_trace ("Intelligent controller\n");
                    break;
                case 0x0F:
                    serial_trace ("Satellite communication controller\n");
                    break;
                case 0x10:
                    serial_trace ("Encryption controller\n");
                    break;
                case 0x11:
                    serial_trace ("Signal processing controller\n");
                    break;
                case 0x12:
                    serial_trace ("Processing accelerators\n");
                    break;
                case 0x13:
                    serial_trace ("Non-Essential Instrumentation\n");
                    break;
                case 0x40:
                    serial_trace ("Co-processor\n");
                    break;
                case 0xFF:
                    serial_trace (
                        "Device does not fit in any defined class\n");
                    break;
                default:
                    serial_trace ("Unknown class\n");
                    break;
                }
            break;
        case 0x01:
            switch (pci->subclass)
                {
                case 0x00:
                    serial_trace ("SCSI storage controller\n");
                    break;
                case 0x01:
                    serial_trace ("IDE interface\n");
                    break;
                }
            break;
        case 0x02:
            switch (pci->subclass)
                {
                case 0x00:
                    serial_trace ("Ethernet controller\n");
                    break;
                }
            break;
        case 0xC:
            switch (pci->subclass)
                {
                case 0x3:
                    switch (pci->prog_if)
                        {
                        case 0x20:
                            serial_trace ("EHCI device \n");
                            pci->service.init = ehci_init;
                            break;
                        }
                    break;
                }
            break;
        }

    ioforge_register_service ((struct IoForgeService *)pci);
}

static void
pci_check_bus (size_t bus)
{
    for (size_t device = 0; device < 32; device++)
        {
            uint16_t vendor_id = pci_readw (bus, device, 0, 0);
            if (vendor_id == 0xFFFF)
                continue;

            uint8_t header_type = pci_readw (bus, device, 0, 0x0E) & 0xFF;

            if (header_type & 0x80)
                {
                    for (size_t func = 0; func < 8; func++)
                        {
                            uint16_t vendor_id
                                = pci_readw (bus, device, func, 0);
                            if (vendor_id == 0xFFFF)
                                continue;

                            serial_trace ("PCI device found at %d:%d:%d\n",
                                          bus, device, func);
                            //  check if it is a PCI to PCI bridge
                            pci_check_func (bus, device, func);
                        }
                }
            else
                {
                    serial_trace ("PCI device found at %d:%d\n", bus, device);
                    pci_gather_info (bus, device, 0);
                }
        }
}

void
pci_scan ()
{
    uint8_t header_type = pci_readw (0, 0, 0, 0x0E) & 0xFF;
    size_t pci_allocator_estimate_size = (32 * 8 * sizeof (struct IoForgePCI))
                                         + sizeof (struct buddy_allocator);
    pci_allocator = buddy_allocator_install (
        VIRT2PHYS ((uintptr_t)phys_base_alloc (
            1 + pci_allocator_estimate_size / 4096)),
        4096 + pci_allocator_estimate_size / 4096);

    if (header_type & 0x80)
        {
            for (size_t func = 0; func < 8; func++)
                {
                    uint16_t vendor_id = pci_readw (0, 0, func, 0);
                    if (vendor_id == 0xFFFF)
                        break;
                    pci_check_bus (func);
                }
        }
    else
        {
            pci_check_bus (0);
        }
}
