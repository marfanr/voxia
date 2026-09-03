#include "autoconf.h"
#include "init.h"
#include "modules/voxmo.h"

INIT(ModuleRegister) {
	// reg kernel module
	vxSetDefaultVoxmoPath(VOXIA_DEFAULT_VOXMO_PATH);
	vxVoxmoInstall("virtio-gpu");
	vxVoxmoInstall("e1000");
	vxVoxmoInstall("ehci");
	vxVoxmoInstall("xhci");
	vxVoxmoInstall("ahci");
	vxVoxmoInstall("atapi");
	vxVoxmoInstall("sata");
	vxVoxmoInstall("usb-hid");
	vxVoxmoReload();
}

INIT(ProccessRegister) {
}