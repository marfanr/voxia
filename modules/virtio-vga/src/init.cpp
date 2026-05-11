#include "virtio-vga/virtio-vga.hpp"
#include <ioforge/ioforge.hpp>

// EHCIModule::EHCIModule() : IoForge("EHCI")
// {
// }

// void
// EHCIModule::setup()
// {
//     device = pci.find_device(EHCI_VENDOR_ID, EHCI_DEVICE_ID);
//     if (!device)
//     {
//         return;
//     }

//     uintptr_t bar        = device->bar[0].address;
//     uint8_t   cap_length = *(uint8_t *)(bar);
//     ehci_op              = (struct ehci_operation *)(bar + cap_length);

//     stop_device();
//     reset_device();
//     ehci_op->frindex       = 0;
//     ehci_op->ctrldssegment = 0;
//     // ehci_op->usbsts = 0x3f;

//     ehci_op->usbcmd |= EHCI_1_MICRO_FRAME | (0b00 << 2);

//     start_device();

//     hcsparam = (uint32_t *)(bar + 0x4);
//     hccparam = (uint32_t *)(bar + 0x8);

//     log(mod, "EHCI is 64 bit : %B", *hccparam & 1);

//     ehci_op->configflag = 1;

//     log(mod, "EHCI setup done");
// }

// void
// EHCIModule::load()
// {
//     // setup queue
//     // qh1 = (ehci_queue_head *)dma_alloc(sizeof(ehci_queue_head));
//     // qh2 = (ehci_queue_head *)dma_alloc(sizeof(ehci_queue_head));

//     // init_periodic();

//     ehci_op->usbsts  = 0x3f;
//     ehci_op->usbintr = 0x1F;
//     // probe();

//     log(mod, "Loaded Module");
// }

// IoForgeModuleConstructor(EHCIModule)
