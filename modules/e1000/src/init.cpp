#include "e1000/e1000.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.hpp"
#include "ioforge/ioforge_pci.hpp"
#include <cstdint>
#include <ioforge/ioforge.hpp>
#include <stdint.h>

IoForgeModuleConstructor(E1000Module);

E1000Module::E1000Module() : IOforgePCI("E1000")
{
}

void
E1000Module::unload()
{
}

E1000Module *
E1000Module::getInstance()
{
    return &instance;
}

static const char hexmap[] = "0123456789ABCDEF";

void
E1000Module::load()
{
    device = find_device(E1000_VENDOR_ID, 0x100e);
    if (!device)
    {
        log(mod, "Device not found");
        return;
    }
    log(mod, "Device found");

    if (detectEeprom())
    {
        log(mod, "Eprom found");
    }

    syncMacAddress();
    char     outc[18] = {0};
    uint8_t *out      = (uint8_t *)outc;
    for (int i = 0; i < 6; i++)
    {
        uint8_t byte = mac_addr[i];
        *out++       = hexmap[(byte >> 4) & 0x0F]; // nibble tinggi
        *out++       = hexmap[byte & 0x0F];        // nibble rendah
        if (i != 5)
            *out++ = ':'; // tambahkan pemisah
    }
    *out = '\0';
    log(mod, "%s", outc);

    enableInterrupt();

    IOUtils::isr_map(11, 0x56);
    IOUtils::irq_register(0x56, (void *)E1000Module::fireHandler);

    linkup();
    for (int i = 0; i < 0x80; i++)
        write(0x5200 + i * 4, 0);

    write(0x00d0, 0x1F6DC);
    write(0x00d0, 0xff & ~4);
    read(0xc0);

    initReceiverX();
    initTransmitterX();
    log(mod, "Successfully Initialized Module");
}

static int
E1000SendPacketCWrapper(const void *data, size_t len)
{
    return instance.sendPacket(data, len);
}

static int
E1000GetMacAddressCWrapper(uint8_t mac[6])
{
    return instance.getMacAddress(mac);
}

static int
E1000ReceivePacketCWraper(void **buffer, size_t *size)
{
    return instance.receivePacket(buffer, size);
}

__attribute__((constructor)) static void
ehci_constructor()
{
    ioforge_nic_service *nic =
        (ioforge_nic_service *)IoForge::IOUtils::alloc(sizeof(ioforge_nic_service));
    nic->ops = (ioforge_nic_operation *)IoForge::IOUtils::alloc(sizeof(ioforge_nic_operation));
    const char *service_name = "E1000";
    IoForge::IOUtils::strcopy((char *)nic->service.name, (char *)service_name);
    nic->ops->send            = E1000SendPacketCWrapper;
    nic->ops->receive         = E1000ReceivePacketCWraper;
    nic->ops->get_mac_address = E1000GetMacAddressCWrapper;
    IoForgeNIC::create(nic);
}
