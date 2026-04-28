#include "ioforge/ioforge_nic.h"
#include "ioforge/ioforge.h"
#include "libk/serial.h"
#include "libk/str.h"
#include "libk/type.h"
#include "memory/memory_utils.h"

extern struct ioforge_service* ioforge_services;

KERNEL_API void IOforgeRegisterNIC(struct ioforge_nic_service* nic) {
	struct ioforge_service* svc =
	    container_of(nic, struct ioforge_nic_service, service);
	svc->type = IOFORGE_NIC;

	LOG2_DEBUG("NIC", "registered NIC at 0x%x (%s)", nic, svc->name);
	ioforge_register_service(svc);
}

KERNEL_API
struct ioforge_nic_service* IOforgeNICFindByName(char* name) {
	struct ioforge_service* tmp = ioforge_services;
	while (tmp != 0) {
		if (tmp->type == IOFORGE_NIC) {
			struct ioforge_nic_service* tmp_nic =
			    (struct ioforge_nic_service*)tmp;
			if (strncmp(tmp_nic->service.name, name,
			            strlen(name)) == 0) {
				serial2_printf("found service type %d (%s)\n",
				               tmp_nic->service.type,
				               tmp_nic->service.name);
				return tmp_nic;
			}
		}
		tmp = tmp->next;
	}
	return 0;
}