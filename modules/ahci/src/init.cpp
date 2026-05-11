#include "ahci/ahci.hpp"
#include <ioforge/ioforge.hpp>

AHCIModule::AHCIModule() : IOforgePCI("AHCI")
{
}

void
AHCIModule::unload()
{
}

void
AHCIModule::load()
{
    device = findDevice(0x8086, 0x2922);
    if (!device)
    {
        log(mod, "Device not found");
        return;
    }
    log(mod, "Device found");
    setup();

    log(mod, "Module Loaded");
}

IoForgeModuleConstructor(AHCIModule);