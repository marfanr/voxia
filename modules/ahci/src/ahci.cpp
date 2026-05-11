#include "ahci/ahci.hpp"
#include "ahci/ahci_reg.hpp"
#include "ahci/block_impl.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_block.hpp"
#include <stdint.h>

typedef struct
{
    uintptr_t clb;
    uintptr_t fb;
    uintptr_t cmd[32];
} ahci_internal_vaddr_t;

// assume max port is 32
static ahci_internal_vaddr_t port_vaddr[32];

static bool
is_device_present(ahci_port_t *port)
{
    uint32_t ssts = port->ssts;
    uint8_t  det  = ssts & HBA_PxSSTS_DET_MASK;

    return (det == HBA_PxSSTS_DET_ESTABLISHED);
}

static ahci_device_type_t
get_device_type(ahci_port_t *port)
{

    if (!is_device_present(port))
    {
        return AHCI_DEV_NULL;
    }
    uint32_t sig = port->sig;

    switch (sig)
    {
        case AHCI_SIG_ATA:
            return AHCI_DEV_SATA;
        case AHCI_SIG_ATAPI:
            return AHCI_DEV_SATAPI;
        case AHCI_SIG_SEMB:
            return AHCI_DEV_SEMB;
        case AHCI_SIG_PM:
            return AHCI_DEV_PM;
        default:
            return AHCI_DEV_NULL;
    }
}

static void
port_power_off(ahci_port_t *port)
{
    port->cmd &= ~1;
    port->cmd &= ~(1 << 4);

    while (1)
    {
        IOForge::IOUtils::sleep(1000);
        if (port->cmd & (1 << 14))
            continue;
        if (port->cmd & (1 << 15))
            continue;

        break;
    }
}

static void
port_power_on(ahci_port_t *port)
{
    while (port->cmd & (1 << 15))
        ;

    port->cmd |= 1 << 4;
    port->cmd |= 1;
}

static int
find_cmdslot(ahci_port_t *port)
{
    // If not set in SACT and CI, the slot is free
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++)
    {
        if ((slots & 1) == 0)
            return i;
        slots >>= 1;
    }
    log("AHCI", "Cannot find free command list entry\n");
    return -1;
}

static bool
ahci_read(ahci_op_t *op, uint16_t port, uint32_t startl, uint32_t starth, uint32_t count,
          uint16_t *buf)
{
    ahci_port_t *p = &op->ports[port];
    p->is          = (uint32_t)-1;
    int freeslot   = find_cmdslot(p);
    log("AHCI", "found free slot at %d", freeslot);
    if (freeslot == -1)
        return false;

    ahci_cmd_t *cmd = (ahci_cmd_t *)port_vaddr[port].clb;
    cmd->cfl        = sizeof(ahci_fis_h2d_t) / sizeof(uint32_t); // Command FIS size
    cmd->w          = 0;                                         // Read from device
    cmd->prdtl      = (uint16_t)((count - 1) >> 4) + 1;          // PRDT entries count
    cmd->a          = 1;                                         // atapi

    ahci_cmd_tbl_t *cmdtbl = (ahci_cmd_tbl_t *)(port_vaddr[port].cmd[freeslot]);
    IOForge::IOUtils::memset(cmdtbl, 0,
                             sizeof(ahci_cmd_tbl_t) + (cmd->prdtl - 1) * sizeof(ahci_prdt_t));

    int i = 0;
    for (i = 0; i < cmd->prdtl - 1; i++)
    {
        cmdtbl->prdt[i].dba = (uint32_t)(uintptr_t)buf;
        cmdtbl->prdt[i].dbc =
            8 * 1024 -
            1; // 8K bytes (this value should always be set to 1 less than the actual value)
        cmdtbl->prdt[i].i = 1;
        buf += 4 * 1024; // 4K words
        count -= 16;     // 16 sectors
    }
    // Last entry
    cmdtbl->prdt[i].dba = (uint32_t)(uintptr_t)buf;
    cmdtbl->prdt[i].dbc = (count << 9) - 1; // 512 bytes per sector
    cmdtbl->prdt[i].i   = 1;

    // cmdtbl->acmd
    // Setup command
    ahci_fis_h2d_t *cmdfis = (ahci_fis_h2d_t *)(&cmdtbl->cfis);

    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c        = 1; // Command
    cmdfis->command  = 0xA0;
    cmdfis->featureh = 0;
    cmdfis->featurel = 0;

    cmdfis->lba0   = (uint8_t)startl;
    cmdfis->lba1   = (uint8_t)(startl >> 8);
    cmdfis->lba2   = (uint8_t)(startl >> 16);
    cmdfis->device = 1 << 6; // LBA mode

    cmdfis->lba3 = (uint8_t)(startl >> 24);
    cmdfis->lba4 = (uint8_t)starth;
    cmdfis->lba5 = (uint8_t)(starth >> 8);

    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    // The below loop waits until the port is no longer busy before issuing a new command
    uint32_t spin = 0;
    while ((p->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000)
    {
        spin++;
    }
    if (spin == 1000000)
    {
        log("AHCI", "Port is hung\n");
        return false;
    }

    p->ci = 1 << freeslot; // Issue command

    // Wait for completion
    while (1)
    {
        // In some longer duration reads, it may be helpful to spin on the DPS bit
        // in the PxIS port field as well (1 << 5)
        if ((p->ci & (1 << freeslot)) == 0)
            break;
        if (p->is & (1 << 30)) // Task file error
        {
            log("AHCI", "Read disk error\n");
            return false;
        }
    }

    // Check again
    if (p->is & (1 << 30))
    {
        log("AHCI", "Read disk error\n");
        return false;
    }
    return true;
}

static bool
ahci_atapi(ahci_op_t *op, uint16_t port, uint32_t lba, uint32_t sector_count, uint8_t acmd[16],
           uintptr_t buf)
{
    ahci_port_t *p = &op->ports[port];
    p->is          = (uint32_t)-1;

    int freeslot = find_cmdslot(p);
    log("AHCI", "found free slot at %d", freeslot);
    if (freeslot == -1)
        return false;

    ahci_cmd_t *cmdh = ((ahci_cmd_t *)port_vaddr[port].clb) + freeslot;
    cmdh->cfl        = sizeof(ahci_fis_h2d_t) / 4;
    cmdh->w          = 0; // Read from device
    cmdh->prdtl      = buf ? 1 : 0;
    cmdh->a          = 1; // ATAPI
    cmdh->c          = 1;

    ahci_cmd_tbl_t *cmdtbl = (ahci_cmd_tbl_t *)(port_vaddr[port].cmd[freeslot]);
    IOForge::IOUtils::memset(cmdtbl, 0,
                             sizeof(ahci_cmd_tbl_t) + (cmdh->prdtl - 1) * sizeof(ahci_prdt_t));

    // Setup PRDT
    cmdtbl->prdt[0].dba  = (uint32_t)(buf & 0xFFFFFFFF);
    cmdtbl->prdt[0].dbau = (uint32_t)(buf >> 32);
    cmdtbl->prdt[0].dbc  = (sector_count * 2048) - 1;
    cmdtbl->prdt[0].i    = 1;

    uint8_t *pkt = cmdtbl->acmd;
    IOForge::IOUtils::memset(pkt, 0, 16);
    IOForge::IOUtils::memcpy(acmd, pkt, 16);

    // Setup FIS
    ahci_fis_h2d_t *cmdfis = (ahci_fis_h2d_t *)(&cmdtbl->cfis);
    IOForge::IOUtils::memset(cmdfis, 0, sizeof(ahci_fis_h2d_t));

    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c        = 1;    // Command
    cmdfis->command  = 0xA0; // PACKET command

    cmdfis->featurel = 0x01; // DMA mode
    cmdfis->featureh = 0x00;

    // LBA registers harus 0 untuk PACKET command
    cmdfis->lba0 = 0;
    cmdfis->lba1 = 0;
    cmdfis->lba2 = 0;
    cmdfis->lba3 = 0;
    cmdfis->lba4 = 0;
    cmdfis->lba5 = 0;

    cmdfis->device = 0;

    cmdfis->countl = 0x00; // Byte count low
    cmdfis->counth = 0x00; // Byte count high

    // Wait for port ready
    uint32_t spin = 0;
    while ((p->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000)
    {
        spin++;
    }
    if (spin == 1000000)
    {
        log("AHCI", "Port is hung");
        return false;
    }

    // Clear error registers
    p->serr = p->serr;
    p->is   = (uint32_t)-1;

    log("AHCI", "Issuing command: LBA=%d, sectors=%d", lba, sector_count);
    p->ci = 1 << freeslot;

    // Wait for completion dengan timeout
    spin = 0;
    while (spin < 5000000)
    { // Timeout lebih lama
        if ((p->ci & (1 << freeslot)) == 0)
            break;

        if (p->is & (1 << 30))
        { // Task file error
            log("AHCI", "Task File Error: TFD=0x%x SERR=0x%x IS=0x%x", p->tfd, p->serr, p->is);
            return false;
        }
        spin++;
    }

    if (spin == 5000000)
    {
        log("AHCI", "Command timeout: CI=0x%x IS=0x%x TFD=0x%x", p->ci, p->is, p->tfd);
        return false;
    }

    // Final check
    if (p->is & (1 << 30))
    {
        log("AHCI", "ATAPI disk error: TFD=0x%x SERR=0x%x", p->tfd, p->serr);
        return false;
    }

    log("AHCI", "ATAPI completed successfully");
    return true;
}

static bool
ATAPIRead(ahci_op_t *op, uint16_t port, uint32_t lba, uint32_t sector_count, uintptr_t buf)
{
    uint8_t acmd[16] = {0x00};
    acmd[0]          = 0x28; // READ(10)
    acmd[1]          = 0;
    acmd[2]          = (lba >> 24) & 0xFF;
    acmd[3]          = (lba >> 16) & 0xFF;
    acmd[4]          = (lba >> 8) & 0xFF;
    acmd[5]          = (lba >> 0) & 0xFF;
    acmd[6]          = 0;
    acmd[7]          = (sector_count >> 8) & 0xFF;
    acmd[8]          = (sector_count >> 0) & 0xFF;
    acmd[9]          = 0;
    return ahci_atapi(op, port, lba, sector_count, acmd, buf);
}

bool
AHCIModule::ATAPI::testUnitReady(ahci_op_t *op, uint16_t port)
{
    uint8_t acmd[8] = {0x00};
    return ahci_atapi(op, port, 0, 0, acmd, 0);
}

boolean_t
AHCIModule::isDevicePresent(uint16_t port)
{
    ahci_port_t *p = &op->ports[port];
    return is_device_present(p);
}

ahci_device_type_t
AHCIModule::getDeviceType(uint16_t port)
{
    ahci_port_t *p = &op->ports[port];
    return get_device_type(p);
}

void
AHCIModule::setup()
{
    if (!device || !device->bar[5].address)
    {
        log(mod, "Device BAR not found");
        return;
    }

    op = (ahci_op_t *)device->bar[5].address;
    op->ghc |= (1 << 0) | (1 << 1) | (1 << 31);

    log(mod, "AHCI reset ghc");

    if (op->cap & (1 << 31))
    {
        log(mod, "Support 64bit");
    }

    // Check Ports Implemented (PI) register
    uint32_t ports_implemented = op->pi;

    for (int i = 0; i < 32; i++)
    {
        if (ports_implemented & (1 << i))
        {
            // Port i is implemented
            ahci_port_t *port = &op->ports[i];

            port_power_off(port);

            uintptr_t clb_paddr = 0;
            auto      clb       = (uintptr_t)IOForge::IOUtils::DMAAlloc(1024, &clb_paddr);
            IOForge::IOUtils::memset((void *)clb, 0, 1024);
            auto aligned_clb_paddr = (clb_paddr + 1024 - 1) & ~(1024 - 1);
            port->clbu             = (aligned_clb_paddr >> 32) & 0xFFFFFFFF;
            port->clb              = aligned_clb_paddr & 0xFFFFFFFF;
            port_vaddr[i].clb      = clb;

            uintptr_t fb_paddr         = 0;
            auto      fb               = (uintptr_t)IOForge::IOUtils::DMAAlloc(256, &fb_paddr);
            auto      aligned_fb_paddr = (fb_paddr + 256 - 1) & ~(256 - 1);
            port->fb                   = aligned_fb_paddr & 0xFFFFFFFF;
            port->fbu                  = (aligned_fb_paddr >> 32) & 0xFFFFFFFF;
            IOForge::IOUtils::memset((void *)fb, 0, 256);
            port_vaddr[i].fb = fb;

            ahci_cmd_t *cmd = (ahci_cmd_t *)clb;
            for (int j = 0; j < 32; j++)
            {
                cmd[j].prdtl = 8;

                uintptr_t ctba_paddr    = 0;
                auto      ctba          = (uintptr_t)IOForge::IOUtils::DMAAlloc(256, &ctba_paddr);
                port_vaddr[i].cmd[j]    = ctba;
                auto aligned_ctba_paddr = (ctba_paddr + 128 - 1) & ~(128 - 1);
                cmd[j].ctba             = aligned_ctba_paddr & 0xFFFFFFFF;
                cmd[j].ctbau            = (aligned_ctba_paddr >> 32) & 0xFFFFFFFF;
                IOForge::IOUtils::memset((void *)ctba, 0, 256);
            }

            port_power_on(port);

            auto type = get_device_type(port);
            switch (type)
            {
                case AHCI_DEV_SATA:
                    log(mod, "SATA device found on port %d", i);
                    break;
                case AHCI_DEV_SATAPI:
                {
                    log(mod, "SATAPI device found on port %d", i);
                    auto ops = satapi_ops_impl(i);
                    // TODO: Dynamic name
                    if (AHCIModule::ATAPI::testUnitReady(op, i))
                    {
                        log(mod, "unit ready port 2;");
                        IOForgeBlock::create("sba", ops, (void *)port);
                    }
                    break;
                }
                case AHCI_DEV_SEMB:
                    log(mod, "SEMB device found on port %d", i);
                    break;
                case AHCI_DEV_PM:
                    log(mod, "PM device found on port %d", i);
                    break;
                case AHCI_DEV_NULL:
                default:
            }
        }
    }

    // test read
    // uintptr_t buf_paddr = 0;
    // uint16_t *buf       = (uint16_t *)IOForge::IOUtils::DMAAlloc(2048, &buf_paddr);
    // log(mod, "test read on buffer 0x%x", buf_paddr);
    // buf_paddr = (buf_paddr + 4 - 1) & ~(4 - 1);

    // iso9660_pvd *pvd = (iso9660_pvd *)buf;
    // log(mod, "ISO id %s", pvd->id);
    // log(mod, "ISO version %d", pvd->version);
    // log(mod, "ISO system id %s", pvd->system_id);
    // log(mod, "ISO volume id %s", pvd->volume_id);
}
