#include "ahci/ahci.hpp"
#include "ahci/ahci_reg.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_block.h"
#include "ioforge/ioforge_pci.h"
#include "memory/kalloc.h"
#include <type.h>
#include <str.h>

// assume max port is 32
static struct ahci_internal_vaddr port_vaddr[32];

static bool is_device_present(ahci_port_t* port) {
	uint32_t ssts = port->ssts;
	uint8_t det = ssts & HBA_PxSSTS_DET_MASK;

	return (det == HBA_PxSSTS_DET_ESTABLISHED);
}

static ahci_device_type_t get_device_type(ahci_port_t* port) {

	if (!is_device_present(port)) {
		return AHCI_DEV_NULL;
	}
	uint32_t sig = port->sig;

	switch (sig) {
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

void AHCIModule::port_power_off(ahci_port_t* port) {
	port->cmd &= ~1;	// Clear ST
	port->cmd &= ~(1 << 4); // Clear FRE

	int timeout = 500; // max 500ms
	while (timeout--) {
		IOForge::IOUtils::sleep(1);
		if (!(port->cmd & (1 << 14)) && !(port->cmd & (1 << 15)))
			break;
	}
	if (timeout <= 0)
		log(mod, "Warning: port stop timeout");
}

void AHCIModule::port_power_on(ahci_port_t* port) {
	while (port->cmd & (1 << 15))
		;

	port->cmd |= 1 << 4;
	port->cmd |= 1;
}

static int find_cmdslot(ahci_port_t* port) {
	// If not set in SACT and CI, the slot is free
	uint32_t slots = (port->sact | port->ci);
	for (int i = 0; i < 32; i++) {
		if ((slots & 1) == 0)
			return i;
		slots >>= 1;
	}
	log("AHCI", "Cannot find free command list entry\n");
	return -1;
}

boolean_t
AHCIModule::issue_and_wait(ahci_port_t* p, int slot, uint32_t timeout) {
	// The below loop waits until the port is no longer busy before issuing a new command
	uint32_t spin = 0;
	while ((p->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < timeout) {
		spin++;
		ioforge_sleep(100);
	}
	if (spin == timeout) {
		log("AHCI", "Port is hung\n");
		return false;
	}

	p->ci = 1 << slot; // Issue command

	// Wait for completion
	while (1) {
		if ((p->ci & (1 << slot)) == 0)
			break;
		if (p->is & (1 << 30)) // Task file error
		{
			log("AHCI", "Read disk error\n");
			return false;
		}
	}

	// Check again
	if (p->is & (1 << 30)) {
		log("AHCI", "Read disk error\n");
		return false;
	}
	return true;
}

#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_IDENTIFY 0xEC
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define ATA_CMD_READ_DMA_EXT 0x25
#define ATA_CMD_PACKET 0xA0
#define ATA_CMD_FLUSH_CACHE_EXT 0xEA

bool AHCIModule::ata_rw(ahci_port_t* p, struct ahci_internal_vaddr* vaddr,
			struct ioforge_block_request* req) {
	p->is = (uint32_t) -1;

	int freeslot = find_cmdslot(p);
	// log("AHCI", "found free slot at %d", freeslot);

	if (freeslot == -1)
		return false;

	ahci_cmd_t* cmd = (ahci_cmd_t*) vaddr->clb + freeslot;
	cmd->cfl =
		sizeof(ahci_fis_h2d_t) / sizeof(uint32_t); // Command FIS size
	cmd->w = (req->flags == IOFORGE_BLOCK_OP_WRITE) ? 1 : 0;
	cmd->a = 0; // bukan atapi
	cmd->c = 1; // command
	cmd->prdtl = req->buffer ? 1 : 0;

	ahci_cmd_tbl_t* cmdtbl = (ahci_cmd_tbl_t*) (vaddr->cmd[freeslot]);
	memset(cmdtbl, 0,
	       sizeof(ahci_cmd_tbl_t) + (cmd->prdtl - 1) * sizeof(ahci_prdt_t));

	if (req->buffer) {
		cmdtbl->prdt[0].dba =
			(uint32_t) ((uintptr_t) req->buffer & 0xFFFFFFFF);
		cmdtbl->prdt[0].dbau =
			(uint32_t) ((uintptr_t) req->buffer >> 32);
		cmdtbl->prdt[0].dbc = (req->block_count * 512) - 1;
		cmdtbl->prdt[0].i = 1;
	}

	ahci_fis_h2d_t* cmdfis = (ahci_fis_h2d_t*) (&cmdtbl->cfis);
	memset(cmdfis, 0, sizeof(ahci_fis_h2d_t));

	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1; // Command
	cmdfis->command = (req->op == IOFORGE_BLOCK_OP_WRITE)
				  ? ATA_CMD_WRITE_DMA_EXT
				  : ATA_CMD_READ_DMA_EXT;
	cmdfis->featureh = 0;
	cmdfis->featurel = 0;

	cmdfis->lba0 = (uint8_t) (req->lba >> 0) & 0xFF;
	cmdfis->lba1 = (uint8_t) (req->lba >> 8) & 0xFF;
	cmdfis->lba2 = (uint8_t) (req->lba >> 16) & 0xFF;
	cmdfis->device = 1 << 6; // LBA mode

	cmdfis->lba3 = (uint8_t) (req->lba >> 24) & 0xFF;
	cmdfis->lba4 = (uint8_t) (req->lba >> 32) & 0xFF;
	cmdfis->lba5 = (uint8_t) (req->lba >> 40) & 0xFF;

	cmdfis->countl = req->block_count & 0xFF;
	cmdfis->counth = (req->block_count >> 8) & 0xFF;

	return issue_and_wait(p, freeslot,
			      req->timeout_ms ? req->timeout_ms : 30000);
}

int AHCIModule::atapi_packet(ahci_port_t* p, struct ahci_internal_vaddr* vaddr,
			     struct ioforge_block_request* req) {
	if (!req->packet_cmd || !req->packet_cmd_len
	    || req->packet_cmd_len > 16)
		return -1;

	p->is = -1;

	int freeslot = find_cmdslot(p);

	if (freeslot == -1)
		return 0;

	ahci_cmd_t* cmd = (ahci_cmd_t*) vaddr->clb + freeslot;
	cmd->cfl =
		sizeof(ahci_fis_h2d_t) / sizeof(uint32_t); // Command FIS size
	cmd->w = (req->flags == IOFORGE_BLOCK_OP_WRITE) ? 1 : 0;
	cmd->a = 1; // atapi
	cmd->c = 1; // command
	cmd->prdtl = req->buffer ? 1 : 0;

	ahci_cmd_tbl_t* cmdtbl = (ahci_cmd_tbl_t*) (vaddr->cmd[freeslot]);

	memset(cmdtbl, 0,
	       sizeof(ahci_cmd_tbl_t) + (cmd->prdtl - 1) * sizeof(ahci_prdt_t));

	if (req->buffer) {
		cmdtbl->prdt[0].dba =
			(uint32_t) ((uintptr_t) req->buffer & 0xFFFFFFFF);
		cmdtbl->prdt[0].dbau =
			(uint32_t) ((uintptr_t) req->buffer >> 32);
		cmdtbl->prdt[0].dbc = req->buffer_size - 1;
		cmdtbl->prdt[0].i = 1;
	}
	memcopy((void*) cmdtbl->acmd, req->packet_cmd, req->packet_cmd_len);

	ahci_fis_h2d_t* cmdfis = (ahci_fis_h2d_t*) (&cmdtbl->cfis);
	memset(cmdfis, 0, sizeof(ahci_fis_h2d_t));

	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1; // Command
	cmdfis->command = ATA_CMD_PACKET;
	cmdfis->featurel = (req->flags & IOFORGE_FLAG_DMA) ? 1 : 0;
	cmdfis->featureh = 0;

	cmdfis->lba2 = 0xFF;
	cmdfis->lba3 = 0xFF;

	return issue_and_wait(p, freeslot,
			      req->timeout_ms ? req->timeout_ms : 30000);
}

int AHCIModule::ata_identify(ahci_port_t* p, struct ahci_internal_vaddr* vaddr,
			     struct ioforge_block_request* req) {
	p->is = (uint32_t) -1;

	int freeslot = find_cmdslot(p);

	if (freeslot == -1)
		return false;

	ahci_cmd_t* cmd = (ahci_cmd_t*) vaddr->clb + freeslot;
	cmd->cfl =
		sizeof(ahci_fis_h2d_t) / sizeof(uint32_t); // Command FIS size
	cmd->w = 0; // read
	cmd->a = 0; // not atapi
	cmd->c = 1; // command
	cmd->prdtl = req->buffer ? 1 : 0;

	ahci_cmd_tbl_t* cmdtbl = (ahci_cmd_tbl_t*) (vaddr->cmd[freeslot]);
	memset(cmdtbl, 0,
	       sizeof(ahci_cmd_tbl_t) + (cmd->prdtl - 1) * sizeof(ahci_prdt_t));

	if (req->buffer) {
		cmdtbl->prdt[0].dba =
			(uint32_t) ((uintptr_t) req->buffer & 0xFFFFFFFF);
		cmdtbl->prdt[0].dbau =
			(uint32_t) ((uintptr_t) req->buffer >> 32);
		cmdtbl->prdt[0].dbc = (req->block_count * 512) - 1;
		cmdtbl->prdt[0].i = 1;
	}

	ahci_fis_h2d_t* cmdfis = (ahci_fis_h2d_t*) (&cmdtbl->cfis);
	memset(cmdfis, 0, sizeof(ahci_fis_h2d_t));

	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1; // Command
	cmdfis->command = ATA_CMD_IDENTIFY_PACKET;
	cmdfis->featureh = 0;
	cmdfis->featurel = 0;

	cmdfis->lba0 = (uint8_t) (req->lba >> 0) & 0xFF;
	cmdfis->lba1 = (uint8_t) (req->lba >> 8) & 0xFF;
	cmdfis->lba2 = (uint8_t) (req->lba >> 16) & 0xFF;
	cmdfis->device = 1 << 6; // LBA mode

	cmdfis->lba3 = (uint8_t) (req->lba >> 24) & 0xFF;
	cmdfis->lba4 = (uint8_t) (req->lba >> 32) & 0xFF;
	cmdfis->lba5 = (uint8_t) (req->lba >> 40) & 0xFF;

	cmdfis->countl = req->block_count & 0xFF;
	cmdfis->counth = (req->block_count >> 8) & 0xFF;

	return issue_and_wait(p, freeslot,
			      req->timeout_ms ? req->timeout_ms : 30000);
}

int AHCIModule::ata_flush(ahci_port_t* p, struct ahci_internal_vaddr* vaddr,
			  struct ioforge_block_request* req) {

	if (!req->packet_cmd || !req->packet_cmd_len
	    || req->packet_cmd_len > 16)
		return -1;

	int freeslot = find_cmdslot(p);
	// log("AHCI", "found free slot at %d", freeslot);

	if (freeslot == -1)
		return false;

	ahci_cmd_t* cmd = (ahci_cmd_t*) vaddr->clb + freeslot;

	cmd->cfl = sizeof(ahci_fis_h2d_t) / 4;
	cmd->w = 0;
	cmd->a = 0;
	cmd->c = 1;
	cmd->prdtl = 0; // tidak ada data transfer

	ahci_cmd_tbl_t* cmdtbl = (ahci_cmd_tbl_t*) (vaddr->cmd[freeslot]);
	memset(cmdtbl, 0,
	       sizeof(ahci_cmd_tbl_t) + (cmd->prdtl - 1) * sizeof(ahci_prdt_t));

	ahci_fis_h2d_t* cmdfis = (ahci_fis_h2d_t*) (&cmdtbl->cfis);
	memset(cmdfis, 0, sizeof(ahci_fis_h2d_t));

	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1;
	cmdfis->command = 0xEA; // FLUSH CACHE EXT

	// Flush bisa lambat (beberapa detik pada HDD besar)
	// timeout lebih panjang dari read/write biasa
	return issue_and_wait(p, freeslot,
			      req->timeout_ms ? req->timeout_ms : 30000);
}

int AHCIModule::submit_impl(struct ioforge_block_device* dev,
			    struct ioforge_block_request* req) {

	// TODO: wait until ready

	ahci_port_t* p = &op->ports[dev->port];
	p->is = (uint32_t) -1;

	switch (req->op) {
	case IOFORGE_BLOCK_OP_READ:
	case IOFORGE_BLOCK_OP_WRITE: {
		return ata_rw(p, &port_vaddr[dev->port], req);
		break;
	}
	case IOFORGE_BLOCK_OP_PACKET: {
		log(mod, "request packet found type");
		return atapi_packet(p, &port_vaddr[dev->port], req);
		break;
	}
	case IOFORGE_BLOCK_OP_IDENTIFY: {
		return ata_identify(p, &port_vaddr[dev->port], req);
		break;
	}
	case IOFORGE_BLOCK_OP_FLUSH: {
		break;
	}
	default:
		return -1;
	}

	// log("AHCI", "ATAPI completed successfully");
	return 1;
}

extern "C" int
submit(struct ioforge_block_device* dev, struct ioforge_block_request* req) {
	auto instance = AHCIModule::getInstance();
	return instance->submit_impl(dev, req);
}

void AHCIModule::port_configure(ahci_port_t* port,
				struct ahci_internal_vaddr* vaddr) {
	port_power_off(port);

	// setupping command list base address (1kb aligne)
	uintptr_t clb_phys_addr = 0;
	auto clb = (uintptr_t) IOForge::IOUtils::DMAAlloc(1024, &clb_phys_addr);
	IOForge::IOUtils::memset((void*) clb, 0, 1024);
	auto aligned_clb_paddr = (clb_phys_addr + 1024 - 1) & ~(1024 - 1);

	port->clbu = (aligned_clb_paddr >> 32) & 0xFFFFFFFF;
	port->clb = aligned_clb_paddr & 0xFFFFFFFF;
	vaddr->clb = clb;

	// setupping FIS base address (256 byte alligned)
	uintptr_t fb_paddr = 0;
	auto fb = (uintptr_t) IOForge::IOUtils::DMAAlloc(256, &fb_paddr);
	auto aligned_fb_paddr = (fb_paddr + 256 - 1) & ~(256 - 1);

	port->fb = aligned_fb_paddr & 0xFFFFFFFF;
	port->fbu = (aligned_fb_paddr >> 32) & 0xFFFFFFFF;
	IOForge::IOUtils::memset((void*) fb, 0, 256);
	vaddr->fb = fb;

	ahci_cmd_t* cmd = (ahci_cmd_t*) clb;
	for (int j = 0; j < 32; j++) {
		cmd[j].prdtl = 8;

		uintptr_t ctba_paddr = 0;
		auto ctba = (uintptr_t) IOForge::IOUtils::DMAAlloc(256,
								   &ctba_paddr);
		vaddr->cmd[j] = ctba;
		auto aligned_ctba_paddr = (ctba_paddr + 128 - 1) & ~(128 - 1);
		cmd[j].ctba = aligned_ctba_paddr & 0xFFFFFFFF;
		cmd[j].ctbau = (aligned_ctba_paddr >> 32) & 0xFFFFFFFF;
		IOForge::IOUtils::memset((void*) ctba, 0, 256);
	}

	port_power_on(port);
}

void AHCIModule::probe() {
	// Check Ports Implemented (PI) register
	uint32_t ports_implemented = op->pi;

	for (int i = 0; i < 32; i++) {
		if (ports_implemented & (1 << i)) {
			log(mod, "Port %d implemented", i);

			// Port i is implemented
			ahci_port_t* port = &op->ports[i];

			port_power_on(port);

			auto vaddr = &port_vaddr[i];

			{
				boolean_t present = false;
				int spin = 0;
				while (spin++ < 3000) {
					IOUtils::sleep(10);
					if (is_device_present(port)) {
						present = true;
						break;
					}
				}
				if (!present)
					break;
			}

			port_configure(port, vaddr);

			auto type = get_device_type(port);

			switch (type) {
			case AHCI_DEV_SATA:
				log(mod, "SATA device found on port %d", i);
				break;
			case AHCI_DEV_SATAPI: {
				log(mod, "SATAPI device found on port %d", i);
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
				break;
			}

			if (!type)
				continue;

			// port->vendor

			// registering block
			{
				struct ioforge_block_device* dev =
					(struct ioforge_block_device*) kalloc(
						sizeof(*dev));

				dev->port = i;
				dev->ops.submit = submit;

				switch (type) {
				case AHCI_DEV_SATA: {
					strcpy((char*) dev->base.name, "SATA");
					dev->type = IOFORGE_BLOCK_TYPE_SATA;
					break;
				}

				case AHCI_DEV_SATAPI: {
					strcpy((char*) dev->base.name,
					       "SATAPI");
					dev->type = IOFORGE_BLOCK_TYPE_SATAPI;
					break;
				}

				case AHCI_DEV_SEMB: {
					strcpy((char*) dev->base.name, "SEMB");
					break;
				}

				case AHCI_DEV_PM: {
					strcpy((char*) dev->base.name, "PM");
					break;
				}

				case AHCI_DEV_NULL:
				default:
					break;
				}

				dev->base.type = IOFORGE_BLOCK;

				ioforge_attach(ioforge_get_block_devices_root(),
					       &dev->base);
			}
		}
	}
}

void AHCIModule::setup() {
	if (!dev_ || !dev_->bar[5].address) {
		log(mod, "Device BAR not found");
		return;
	}

	op = (ahci_op_t*) dev_->bar[5].address;
	op->ghc |= (1 << 0) | (1 << 1) | (1 << 31);

	log(mod, "AHCI reset ghc");

	if (op->cap & (1 << 31)) {
		log(mod, "Support 64bit");
	}

	// setup ops
	probe();

	// test read
	// uintptr_t buf_paddr = 0;
	// uint8_t* buf = (uint8_t*) IOForge::IOUtils::DMAAlloc(2048, &buf_paddr);
	// log(mod, "test read on buffer 0x%x", buf_paddr);

	// Align both buf and buf_paddr consistently
	// uintptr_t aligned_paddr = (buf_paddr + 4 - 1) & ~(4 - 1);
	// uintptr_t offset = aligned_paddr - buf_paddr;
	// uint8_t* aligned_buf = buf + offset;

	// ATAPIRead(op, 1, 0, 16, aligned_paddr);

	// for (int i = 0; i < 100; i++) {
	// 	serial2_printf("%x ", aligned_buf[i]);
	// }
	// serial2_printf("\n");
	// iso9660_pvd *pvd = (iso9660_pvd *)buf;
	// log(mod, "ISO id %s", pvd->id);
	// log(mod, "ISO version %d", pvd->version);
	// log(mod, "ISO system id %s", pvd->system_id);
	// log(mod, "ISO volume id %s", pvd->volume_id);
}

// bool AHCIModule::ATAPI::testUnitReady(ahci_op_t* op, uint16_t port) {
// 	uint8_t acmd[8] = {0x00};
// 	return ahci_atapi(op, port, 0, 0, acmd, 0);
// }

// boolean_t AHCIModule::isDevicePresent(uint16_t port) {
// 	ahci_port_t* p = &op->ports[port];
// 	return is_device_present(p);
// }

// ahci_device_type_t AHCIModule::getDeviceType(uint16_t port) {
// 	ahci_port_t* p = &op->ports[port];
// 	return get_device_type(p);
// }

// static bool ahci_atapi(ahci_op_t* op, uint16_t port, uint32_t lba,
// 		       uint32_t sector_count, uint8_t acmd[16], uintptr_t buf) {
// 	ahci_port_t* p = &op->ports[port];
// 	p->is = (uint32_t) -1;

// 	int freeslot = find_cmdslot(p);
// 	log("AHCI", "found free slot at %d", freeslot);
// 	if (freeslot == -1)
// 		return false;

// 	ahci_cmd_t* cmdh = ((ahci_cmd_t*) port_vaddr[port].clb) + freeslot;
// 	cmdh->cfl = sizeof(ahci_fis_h2d_t) / 4;
// 	cmdh->w = 0; // Read from device
// 	cmdh->prdtl = buf ? 1 : 0;
// 	cmdh->a = 1; // ATAPI
// 	cmdh->c = 1;

// 	ahci_cmd_tbl_t* cmdtbl =
// 		(ahci_cmd_tbl_t*) (port_vaddr[port].cmd[freeslot]);
// 	IOForge::IOUtils::memset(cmdtbl, 0,
// 				 sizeof(ahci_cmd_tbl_t)
// 					 + (cmdh->prdtl - 1)
// 						   * sizeof(ahci_prdt_t));

// 	// Setup PRDT
// 	cmdtbl->prdt[0].dba = (uint32_t) (buf & 0xFFFFFFFF);
// 	cmdtbl->prdt[0].dbau = (uint32_t) (buf >> 32);
// 	cmdtbl->prdt[0].dbc = (sector_count * 2048) - 1;
// 	cmdtbl->prdt[0].i = 1;

// 	uint8_t* pkt = cmdtbl->acmd;
// 	IOForge::IOUtils::memset(pkt, 0, 16);
// 	IOForge::IOUtils::memcpy(pkt, acmd, 16);

// 	// Setup FIS
// 	ahci_fis_h2d_t* cmdfis = (ahci_fis_h2d_t*) (&cmdtbl->cfis);
// 	IOForge::IOUtils::memset(cmdfis, 0, sizeof(ahci_fis_h2d_t));

// 	cmdfis->fis_type = FIS_TYPE_REG_H2D;
// 	cmdfis->c = 1;		// Command
// 	cmdfis->command = 0xA0; // PACKET command

// 	cmdfis->featurel = 0x01; // DMA mode
// 	cmdfis->featureh = 0x00;

// 	// LBA registers harus 0 untuk PACKET command
// 	cmdfis->lba0 = 0;
// 	cmdfis->lba1 = 0;
// 	cmdfis->lba2 = 0;
// 	cmdfis->lba3 = 0;
// 	cmdfis->lba4 = 0;
// 	cmdfis->lba5 = 0;

// 	cmdfis->device = 0;

// 	cmdfis->countl = 0x00; // Byte count low
// 	cmdfis->counth = 0x00; // Byte count high

// 	// Wait for port ready
// 	uint32_t spin = 0;
// 	while ((p->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) {
// 		spin++;
// 	}
// 	if (spin == 1000000) {
// 		log("AHCI", "Port is hung");
// 		return false;
// 	}

// 	// Clear error registers
// 	p->serr = p->serr;
// 	p->is = (uint32_t) -1;

// 	log("AHCI", "Issuing command: LBA=%d, sectors=%d", lba, sector_count);
// 	p->ci = 1 << freeslot;

// 	// Wait for completion dengan timeout
// 	spin = 0;
// 	while (spin < 5000000) { // Timeout lebih lama
// 		if ((p->ci & (1 << freeslot)) == 0)
// 			break;

// 		if (p->is & (1 << 30)) { // Task file error
// 			log("AHCI",
// 			    "Task File Error: TFD=0x%x SERR=0x%x IS=0x%x",
// 			    p->tfd, p->serr, p->is);
// 			return false;
// 		}
// 		spin++;
// 	}

// 	if (spin == 5000000) {
// 		log("AHCI", "Command timeout: CI=0x%x IS=0x%x TFD=0x%x", p->ci,
// 		    p->is, p->tfd);
// 		return false;
// 	}

// 	// Final check
// 	if (p->is & (1 << 30)) {
// 		log("AHCI", "ATAPI disk error: TFD=0x%x SERR=0x%x", p->tfd,
// 		    p->serr);
// 		return false;
// 	}

// 	log("AHCI", "ATAPI completed successfully");
// 	return true;
// }

// static bool ATAPIRead(ahci_op_t* op, uint16_t port, uint32_t lba,
// 		      uint32_t sector_count, uintptr_t buf) {
// 	uint8_t acmd[16] = {0x00};
// 	acmd[0] = 0x28; // READ(10)
// 	acmd[1] = 0;
// 	acmd[2] = (lba >> 24) & 0xFF;
// 	acmd[3] = (lba >> 16) & 0xFF;
// 	acmd[4] = (lba >> 8) & 0xFF;
// 	acmd[5] = (lba >> 0) & 0xFF;
// 	acmd[6] = 0;
// 	acmd[7] = (sector_count >> 8) & 0xFF;
// 	acmd[8] = (sector_count >> 0) & 0xFF;
// 	acmd[9] = 0;
// 	return ahci_atapi(op, port, lba, sector_count, acmd, buf);
// }