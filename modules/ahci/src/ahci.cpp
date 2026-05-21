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
	// Clear ST (bit 0) dulu
	port->cmd &= ~(1UL << 0);

	// Tunggu CR (bit 15) clear
	int timeout = 500;
	while (timeout-- > 0) {
		IOForge::IOUtils::sleep(1);
		if (!(port->cmd & (1UL << 15)))
			break;
	}
	if (timeout <= 0)
		log(mod, "Warning: CR not cleared");

	// Baru clear FRE (bit 4)
	port->cmd &= ~(1UL << 4);

	// Tunggu FR (bit 14) clear
	timeout = 500;
	while (timeout-- > 0) {
		IOForge::IOUtils::sleep(1);
		if (!(port->cmd & (1UL << 14)))
			break;
	}
	if (timeout <= 0)
		log(mod, "Warning: FR not cleared");
}

void AHCIModule::port_power_on(ahci_port_t* port) {
	// Tunggu CR clear sebelum set ST
	int timeout = 500;
	while (timeout-- > 0) {
		if (!(port->cmd & (1UL << 15)))
			break;
		IOForge::IOUtils::sleep(1);
	}

	// Set FRE dulu, baru ST
	port->cmd |= (1UL << 4); // FRE
	IOForge::IOUtils::sleep(1);
	port->cmd |= (1UL << 0); // ST
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
AHCIModule::issue_and_wait(ahci_port_t* p, int slot, uint32_t timeout_ms) {
	uint32_t spin = 0;
	while ((p->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < timeout_ms) {
		spin++;
		ioforge_sleep(1); // 1ms per tick
	}
	if (spin >= timeout_ms) {
		log("AHCI", "Port is hung (pre-issue timeout)");
		return false;
	}

	p->ci = 1 << slot;

	// Timeout untuk menunggu command selesai
	spin = 0;
	while (spin < timeout_ms) {
		if ((p->ci & (1UL << slot)) == 0)
			break;
		if (p->is & (1UL << 30)) {
			log("AHCI",
			    "Task file error: TFD=0x%x SERR=0x%x IS=0x%x",
			    p->tfd, p->serr, p->is);
			return false;
		}

		// Small delay initially, then sleep
		if (spin < 10) {
			for (int i = 0; i < 1000; i++)
				__asm__ volatile("pause");
		} else {
			ioforge_sleep(1);
		}
		spin++;
	}

	if (spin >= timeout_ms) {
		log("AHCI",
		    "Command timeout after %dms (CI=0x%x IS=0x%x TFD=0x%x)",
		    timeout_ms, p->ci, p->is, p->tfd);

		// To correctly cancel a command, the port must be stopped
		port_power_off(p);
		port_power_on(p);
		return false;
	}

	if (p->is & (1UL << 30)) {
		log("AHCI", "Read disk error after completion");
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

void AHCIModule::build_prdt(ahci_cmd_t* cmd, ahci_cmd_tbl_t* cmdtbl,
			    void* buffer, size_t size) {
	if (!buffer || size == 0) {
		cmd->prdtl = 0;
		return;
	}

	uintptr_t addr = (uintptr_t) buffer;
	uint16_t idx = 0;

	while (size > 0 && idx < 8) {
		size_t bytes = (size > 0x400000) ? 0x400000 : size;

		cmdtbl->prdt[idx].dba = (uint32_t) (addr & 0xFFFFFFFF);
		cmdtbl->prdt[idx].dbau = (uint32_t) (addr >> 32);
		cmdtbl->prdt[idx].dbc = (uint32_t) (bytes - 1);
		cmdtbl->prdt[idx].i = 1;

		size -= bytes;
		addr += bytes;
		idx++;
	}

	cmd->prdtl = idx;
}

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
	cmd->w = (req->op == IOFORGE_BLOCK_OP_WRITE) ? 1 : 0;
	cmd->a = 0; // bukan atapi
	cmd->c = 1; // command

	ahci_cmd_tbl_t* cmdtbl = (ahci_cmd_tbl_t*) (vaddr->cmd[freeslot]);
	memset(cmdtbl, 0,
	       sizeof(ahci_cmd_tbl_t) + (8 - 1) * sizeof(ahci_prdt_t));

	build_prdt(cmd, cmdtbl, req->buffer, (size_t) req->block_count * 512);

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

	p->is = (uint32_t) -1;
	p->serr = p->serr;

	int freeslot = find_cmdslot(p);
	if (freeslot == -1)
		return 0;

	bool is_dma = req->flags & IOFORGE_FLAG_DMA;
	bool is_write = req->flags & IOFORGE_FLAG_WRITE;

	ahci_cmd_t* cmd = (ahci_cmd_t*) vaddr->clb + freeslot;
	cmd->cfl = sizeof(ahci_fis_h2d_t) / sizeof(uint32_t);
	cmd->w = is_write ? 1 : 0;
	cmd->a = 1;
	cmd->c = 1;

	ahci_cmd_tbl_t* cmdtbl = (ahci_cmd_tbl_t*) (vaddr->cmd[freeslot]);
	memset(cmdtbl, 0,
	       sizeof(ahci_cmd_tbl_t) + (8 - 1) * sizeof(ahci_prdt_t));

	build_prdt(cmd, cmdtbl, req->buffer, req->buffer_size);

	memcopy((void*) cmdtbl->acmd, req->packet_cmd, req->packet_cmd_len);

	ahci_fis_h2d_t* cmdfis = (ahci_fis_h2d_t*) (&cmdtbl->cfis);
	memset(cmdfis, 0, sizeof(ahci_fis_h2d_t));

	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1;
	cmdfis->command = ATA_CMD_PACKET;
	cmdfis->featureh = 0;

	if (is_dma) {
		cmdfis->featurel = 0x01; // DMA bit
		cmdfis->lba1 = 0;
		cmdfis->lba2 = 0;
	} else {
		cmdfis->featurel = 0x00;
		uint16_t byte_count =
			req->buffer_size
				? (uint16_t) (req->buffer_size & 0xFFFE)
				: 0xFFFE;
		cmdfis->lba1 = (uint8_t) (byte_count & 0xFF); // byte count LOW
		cmdfis->lba2 =
			(uint8_t) ((byte_count >> 8) & 0xFF); // byte count HIGH
	}
	// lba0, lba3, lba4, lba5 = 0 — jangan di-set apapun
	cmdfis->device = 0;

	int r = issue_and_wait(p, freeslot,
			       req->timeout_ms ? req->timeout_ms : 30000);

	return r;
}

int AHCIModule::ata_identify(ahci_port_t* p, struct ahci_internal_vaddr* vaddr,
			     struct ioforge_block_request* req, bool is_atapi) {
	p->is = (uint32_t) -1;

	int freeslot = find_cmdslot(p);

	if (freeslot == -1)
		return false;

	ahci_cmd_t* cmd = (ahci_cmd_t*) vaddr->clb + freeslot;
	cmd->cfl = sizeof(ahci_fis_h2d_t) / sizeof(uint32_t);
	cmd->w = 0;
	cmd->a = 0; // not atapi packet (it's a reg command)
	cmd->c = 1; // command

	ahci_cmd_tbl_t* cmdtbl = (ahci_cmd_tbl_t*) (vaddr->cmd[freeslot]);
	memset(cmdtbl, 0,
	       sizeof(ahci_cmd_tbl_t) + (8 - 1) * sizeof(ahci_prdt_t));

	build_prdt(cmd, cmdtbl, req->buffer, 512);

	ahci_fis_h2d_t* cmdfis = (ahci_fis_h2d_t*) (&cmdtbl->cfis);
	memset(cmdfis, 0, sizeof(ahci_fis_h2d_t));

	cmdfis->fis_type = FIS_TYPE_REG_H2D;
	cmdfis->c = 1; // Command
	cmdfis->command = is_atapi ? ATA_CMD_IDENTIFY_PACKET : ATA_CMD_IDENTIFY;
	cmdfis->device = 0;

	// IDENTIFY commands don't use LBA
	cmdfis->lba0 = 0;
	cmdfis->lba1 = 0;
	cmdfis->lba2 = 0;
	cmdfis->lba3 = 0;
	cmdfis->lba4 = 0;
	cmdfis->lba5 = 0;

	cmdfis->countl = 0;
	cmdfis->counth = 0;

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
		return ata_identify(p, &port_vaddr[dev->port], req,
				    dev->type == IOFORGE_BLOCK_TYPE_SATAPI);
		break;
	}
	case IOFORGE_BLOCK_OP_FLUSH: {
		break;
	}
	default:
		return -1;
	}

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
	auto aligned_clb_paddr = (clb_phys_addr + 1024 - 1) & (uintptr_t)~(1024 - 1);

	port->clbu = (aligned_clb_paddr >> 32) & 0xFFFFFFFF;
	port->clb = aligned_clb_paddr & 0xFFFFFFFF;
	vaddr->clb = clb;

	// setupping FIS base address (256 byte alligned)
	uintptr_t fb_paddr = 0;
	auto fb = (uintptr_t) IOForge::IOUtils::DMAAlloc(256, &fb_paddr);
	auto aligned_fb_paddr = (fb_paddr + 256 - 1) & (uintptr_t)~(256 - 1);

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
		auto aligned_ctba_paddr = (ctba_paddr + 128 - 1) & (uintptr_t)~(128 - 1);
		cmd[j].ctba = aligned_ctba_paddr & 0xFFFFFFFF;
		cmd[j].ctbau = (aligned_ctba_paddr >> 32) & 0xFFFFFFFF;
		IOForge::IOUtils::memset((void*) ctba, 0, 256);
	}

	port_power_on(port);
}

void AHCIModule::probe() {
	// Check Ports Implemented (PI) register
	uint32_t ports_implemented = op->pi;

	for (uint8_t i = 0; i < 32; i++) {
		if (ports_implemented & (1UL << i)) {
			log(mod, "Port %d implemented", i);

			// Port i is implemented
			ahci_port_t* port = &op->ports[i];

			port_power_on(port);

			auto vaddr = &port_vaddr[i];

			{
				boolean_t present = false;
				int spin = 0;
				while (spin++ < 50 && !present) {
					if (is_device_present(port)) {
						present = true;
						break;
					}
					IOUtils::sleep(10);
				}
				if (!present) {
					log(mod, "No device on port %d", i);
					continue;
				}
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

			// registering block
			{
				struct ioforge_block_device* dev =
					(struct ioforge_block_device*) kalloc(
						sizeof(struct
						       ioforge_block_device));
				memset(dev, 0,
				       sizeof(struct ioforge_block_device));

				dev->port = i;
				dev->ops.submit = submit;

				switch (type) {
				case AHCI_DEV_SATA: {
					strcpy((char*) dev->base.name, "SATA");
					dev->base.name[4] = '\0';
					dev->type = IOFORGE_BLOCK_TYPE_SATA;
					break;
				}

				case AHCI_DEV_SATAPI: {
					strcpy((char*) dev->base.name,
					       "SATAPI");
					dev->base.name[6] = '\0';
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

	// Enable AHCI mode dulu
	op->ghc |= (1UL << 31);

	// Reset HBA
	op->ghc |= (1UL << 0);

	// Tunggu reset selesai (bit 0 harus clear sendiri)
	int timeout = 1000;
	while ((op->ghc & (1UL << 0)) && timeout-- > 0) {
		IOUtils::sleep(1);
	}
	if (timeout <= 0) {
		log(mod, "HBA reset timeout");
		return;
	}

	// Enable AHCI + interrupt setelah reset
	op->ghc |= (1UL << 31) | (1UL << 1);

	log(mod, "AHCI reset ghc done");

	if (op->cap & (1UL << 31)) {
		log(mod, "Support 64bit");
	}

	probe();
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
// 		if ((p->ci & (1UL << freeslot)) == 0)
// 			break;

// 		if (p->is & (1UL << 30)) { // Task file error
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
// 	if (p->is & (1UL << 30)) {
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