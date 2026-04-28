#include "autoconf.h"
#include "hal/cpu/interrupt.h"
#include "init.h"
#include "libk/serial.h"
#include "modules/voxmo.h"
#include "sys/library.h"

static void detectBootPartition() {}

INIT(ModuleRegister) {
	// reg library
	// tidak akan dipkaia lagi
	// library_register("/init/lib/libioforge.so", LIBRARY_TYPE_DYNAMIC);

	// reg kernel module
	vxSetDefaultVoxmoPath(VOXIA_DEFAULT_VOXMO_PATH);
	// vxVoxmoInstall("usb-hid");
	vxVoxmoInstall("e1000");
	// vxVoxmoInstall("ehci");

	// register default filesystem : iso96660, fat
	// filesystem_register("ISO9660", 0);
	// vxVoxmoInstall("ahci");
	// detectBootPartition();

	// todo detect boot partition

	// vxVoxmoInstall("intel-hda");

	LOG2_DEBUG("REGISTER", "finish registered all module");
	vxVoxmoReload();
}

INIT(ProccessRegister) {}