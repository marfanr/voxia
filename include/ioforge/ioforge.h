#ifndef __SYS__IOFORGE__IOFORGE_H_
#define __SYS__IOFORGE__IOFORGE_H_

#include <type.h>

#define IOFORGE_SERVICE_NAME_MAX_LEN 64

// ioforge /pci/00:01:03

typedef enum : uint8_t {
	IOFORGE_ROOT = 0xFF,
	IOFORGE_PCI = 0xF1,
	IOFORGE_PCI_BUS = 0xF2,
	IOFORGE_ACPI = 0xAC,
	IOFORGE_USB_DEVICE = 0xA2,
	IOFORGE_NIC = 0xC3,
	IOFORGE_USB_CONTROLLER = 0xE3,
	IOFORGE_VIRTIO = 0xD3,
	IOFORGE_BLOCK = 0xEE,
} IoForgeType;

#define IOFORGE_F_ENABLE 1
#define IOFORGE_F_VIRTIO (1 << 31)

#ifdef __cplusplus
extern "C" {
#endif

struct ioforge_device {
	char name[IOFORGE_SERVICE_NAME_MAX_LEN];
	IoForgeType type;
	uint32_t flags;
	uint32_t address;

	struct ioforge_device* parent;	     /* NULL = root */
	struct ioforge_device* first_child;  /* kepala linked list anak */
	struct ioforge_device* next_sibling; /* sibling kanan */
	struct ioforge_device* prev_sibling; /* sibling kiri (opsional) */

	// struct ioforge_ops* ops;
};

void serial2_printf(const char* fmt, ...);
void* ioforge_dma_alloc(size_t size, uintptr_t* paddr);
void* ioforge_dma_alloc(size_t size, uintptr_t* paddr);
void ioforge_memset(void* ptr, uint8_t value, size_t num);
void ioforge_memcpy(void* dst, void* src, size_t num);
void ioforge_sleep(uint32_t time);
uint16_t ioforge_irq_alloc_entry();
uint32_t ioforge_isr_get_vector(uint8_t irq);
void ioforge_irq_register(uint8_t n, void* handler);
void ioforge_map_isr(uint8_t irq, uint8_t vector);
void serial_printf(const char* fmt, ...);
void* ioforge_alloc(size_t size);
void ioforge_dma_free(void* paddr, void* vaddr, size_t size);
void IOforgeStrCopy(char* dst, char* src);
void IOforgeStrnCopy(char* dst, char* src, size_t len);

uintptr_t IOforgeMMapPhys(uintptr_t paddr, size_t size);
void ioforge_free(void* ptr, size_t size);

void ioforge_attach(struct ioforge_device* parent,
		    struct ioforge_device* child);
struct ioforge_device* ioforge_get_root();
struct ioforge_device*
ioforge_find_by_name(struct ioforge_device* root, const char* name);
bool ioforge_can_contain_pci(IoForgeType type);

struct ioforge_device* ioforge_get_pci_root();
struct ioforge_device* ioforge_get_block_devices_root();

struct ioforge_pci_device*
ioforge_find_pci_device(struct ioforge_device* node, uint16_t vendor_id,
			uint16_t device_id);
void print_device_tree(struct ioforge_device* node, int indent);

#ifdef __cplusplus
}
#endif

#endif // __SYS__IOFORGE__IOFORGE_H_