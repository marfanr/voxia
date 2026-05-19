#ifndef __HAL__ACPI__ACPI_H_
#define __HAL__ACPI__ACPI_H_

#include <type.h>

struct RSDP_t {
	char Signature[8];
	uint8_t Checksum;
	char OEMID[6];
	uint8_t Revision;
	uint32_t RsdtAddress;
} __attribute__((packed));

struct SDT {
	char Signature[4];
	uint32_t Length;
	uint8_t Revision;
	uint8_t Checksum;
	char OEMID[6];
	char OEMTableID[8];
	uint32_t OEMRevision;
	uint32_t CreatorID;
	uint32_t CreatorRevision;
} __attribute__((packed));

struct RSDT {
	struct SDT h;
	uint32_t PointerToOtherSDT[];
} __attribute__((packed));

typedef struct __attribute__((__packed__)) {
	uint8_t entry_type; // according to madt_entry_type_t
	uint8_t record_length;
} madt_record_table_entry_t;

struct MADT {
	struct SDT header;
	uint32_t localApicAddress;
	uint32_t flags;
	madt_record_table_entry_t table[];
} __attribute__((packed));

struct ACPI_APIC_ENTRY {
	uint8_t type;
	uint8_t length;
} __attribute__((packed));

enum ACPI_TABLE_TYPE {
	ACPI_PROCESSOR_LAPIC = 0,
	ACPI_IO_APIC = 1,
	ACPI_IO_INT_OVERRIDE = 2,
	ACPI_NMI = 3,
	ACPI_LOCAL_APIC_NMI = 4,
	ACPI_LOCAL_APIC_OVERRIDE = 5,
	ACPI_LOCAL_X2_APIC = 9
};

struct ACPI_IO_APIC {
	struct ACPI_APIC_ENTRY h;
	uint8_t ioApicId;
	uint8_t reserved;
	uint32_t ioApicAddress;
	uint32_t globalSystemInterruptBase;
} __attribute__((packed));

typedef enum { Off = 0, Active } cpu_core_status_t;

struct cpu_core {
	cpu_core_status_t status;
	uint8_t apicid;
	uint8_t cpuid;
	uint32_t flag;
	struct cpu_core* next;
};
struct cpu_core* vxGetCpuInfo(uint8_t apicid);

uintptr_t acpi_map_phys_page(uintptr_t phys_addr, size_t len);
void acpi_phys_page_unmap(uintptr_t addr);
uint8_t vxGetNumberOfCores();

#endif // __HAL__ACPI__ACPI_H_