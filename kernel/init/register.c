#include "autoconf.h"
#include "init.h"
#include "modules/voxmo.h"

INIT(ModuleRegister) {
	// reg kernel module
	vxSetDefaultVoxmoPath(VOXIA_DEFAULT_VOXMO_PATH);
	vxVoxmoInstall("e1000");
	vxVoxmoInstall("ehci");
	vxVoxmoInstall("xhci");
	vxVoxmoInstall("virtio-gpu");
	vxVoxmoInstall("ahci");
	vxVoxmoInstall("atapi");
	vxVoxmoInstall("usb-hid");
	vxVoxmoReload();
}

INIT(ProccessRegister) {
}