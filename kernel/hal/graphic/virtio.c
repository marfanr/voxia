#include <hal/graphic/virtio.h>
#include <libk/serial.h>
#include <hal/pci/pci.h>

void virtio_gpu_init(struct IoForgePCI *pci_device) {
    serial_trace("capable regster : 0x%x\n", pci_device->capability_ptr);
    size_t offset = pci_device->capability_ptr;

    // uint8_t cap_vendor = (uint8_t)pci_readw(pci_device->pci_bus, pci_device->pci_dev, pci_device->pci_func, offset + 0x00);
    // serial_trace("cap vendor id : 0x%x\n", cap_vendor);
    uint8_t next = (uint8_t)(pci_readw(pci_device->pci_bus, pci_device->pci_dev, pci_device->pci_func, offset) >> 8);
    serial_trace("next : 0x%x\n", next);
    offset = next;

    // / next = (uint8_t)(pci_readw(pci_device->pci_bus, pci_device->pci_dev, pci_device->pci_func, offset) >> 8);
    // serial_trace("next : 0x%x\n", next);
    // offset = next;

    uint8_t cap_vendor = (uint8_t)pci_readw(pci_device->pci_bus, pci_device->pci_dev, pci_device->pci_func, offset + 0x00);
    serial_trace("cap vendor id : 0x%x\n", cap_vendor);
    uint8_t cap_len = (uint8_t)pci_readw(pci_device->pci_bus, pci_device->pci_dev, pci_device->pci_func, offset + 2);
    serial_trace("cap len : %d\n", cap_len);
    uint8_t cap_type = (uint8_t)(pci_readw(pci_device->pci_bus, pci_device->pci_dev, pci_device->pci_func, offset + 2) >> 8);
    serial_trace("cap vendor type : 0x%x\n", cap_type);
    uint8_t bar = (uint8_t)(pci_readw(pci_device->pci_bus, pci_device->pci_dev, pci_device->pci_func, offset + 4));
    serial_trace("bar : 0x%x\n", bar);

    uint32_t bar_offset = (uint32_t)(pci_readw(pci_device->pci_bus, pci_device->pci_dev, pci_device->pci_func, offset + 4));
    bar_offset |= (uint32_t)(pci_readw(pci_device->pci_bus, pci_device->pci_dev, pci_device->pci_func, offset + 5) << 16);
    serial_trace("bar offset : 0x%x\n", bar_offset);
}
