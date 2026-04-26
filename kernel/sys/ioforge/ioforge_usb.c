
#include "ioforge/ioforge_usb.h"
#include "ioforge/ioforge.h"
#include "libk/serial.h"

KERNEL_API
void
ioforge_register_usb_controller(USBController *c)
{
    // vxUSBRegisterController(c);
    LOG_INFO("USB", "registered controller");
}
