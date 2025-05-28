#ifndef __FIRMW__ACPI__ACPI_H_
#define __FIRMW__ACPI__ACPI_H_

#include <libk/stivale2.h>
#include <libk/type.h>

typedef struct
{
    uint8_t apicid;
    uint8_t cpuid;
} cpu_core_t;

void acpi_setup (struct stivale2_struct_tag_rsdp *rsdp_info);
extern uintptr_t local_apic_addr;
extern uint8_t *io_apic_addr;

#endif // __FIRMW__ACPI__ACPI_H_