#ifndef __SYS__IOFORGE__IOFORGE_H_
#define __SYS__IOFORGE__IOFORGE_H_

#include "type.h"

// ioforge /pci/00:01:03

enum IoForgeType : uint8_t
{
    IOFORGE_PCI = 0xF1,
    IOFORGE_USB = 0xA2,
    IOFORGE_NIC = 0xC3,
};

struct ioforge_service
{
    const char              name[255];
    uint64_t                address;
    uint8_t                 type;
    struct ioforge_service *next;
};

#ifdef __cplusplus
extern "C"
{
#endif

    void                        ioforge_register_service(struct ioforge_service *service);
    struct ioforge_pci_service *ioforge_get_pci_device(uint16_t vendor_id, uint16_t device_id);
    void                       *ioforge_dma_alloc(size_t size, uintptr_t *paddr);
    void                        ioforge_memset(void *ptr, uint8_t value, size_t num);
    void                        ioforge_memcpy(void *dst, void *src, size_t num);
    void                        ioforge_sleep(uint32_t time);
    void                        ioforge_irq_register(uint8_t n, void *handler);
    void                        ioforge_map_isr(uint8_t irq, uint8_t vector);
    void                        serial_printf(const char *fmt, ...);
    uint16_t                    coreGetCpuID();
    void                       *ioforge_alloc(size_t size);
    void                        ioforge_dma_free(void *paddr, void *vaddr, size_t size);
    void                        IOforgeStrCopy(char *dst, char *src);
    uintptr_t                   IOforgeMMapPhys(uintptr_t paddr, size_t size);

#ifdef __cplusplus
}
#endif

#endif // __SYS__IOFORGE__IOFORGE_H_