#include "autoconf.h"
#include "init.h"
#include "libk/serial.h"
#include "modules/voxmo.h"
#include "sys/library.h"

INIT(ModuleRegister) {
	// reg library
	// tidak akan dipkaia lagi
	// library_register("/init/lib/libioforge.so", LIBRARY_TYPE_DYNAMIC);

	// reg kernel module

	// vxSetDefaultVoxmoPath(VOXIA_DEFAULT_VOXMO_PATH);
	// vxVoxmoInstall("e1000");
	// vxVoxmoInstall("ehci");
	// vxVoxmoInstall("virtio-gpu");
	// vxVoxmoInstall("ahci");
	// vxVoxmoInstall("atapi");
	// vxVoxmoInstall("usb-hid");

	// // // register default filesystem : iso96660, fat
	// // // filesystem_register("ISO9660", 0);
	// // // vxVoxmoInstall("ahci");
	// // // detectBootPartition();

	// // // todo detect boot partition

	// // // vxVoxmoInstall("intel-hda");
	// vxVoxmoReload();
}

INIT(ProccessRegister) {
}