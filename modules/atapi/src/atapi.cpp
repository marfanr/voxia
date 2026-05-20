#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_block.h"
#include "notify.h"
#include "type.h"
#include "vfs/dentry.h"
#include "vfs/dev.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <atapi/atapi.hpp>
#include <str.h>

typedef struct {
	uint32_t last_lba;
	uint32_t block_size;
} __attribute__((packed)) read_capacity10_resp_t;

void ATAPIModule::build_acmd(uint8_t opcode, uint32_t lba,
                             uint32_t sector_count, uint8_t (&acmd)[16]) {
	acmd[0] = opcode;
	acmd[1] = 0;
	acmd[2] = (lba >> 24) & 0xFF;
	acmd[3] = (lba >> 16) & 0xFF;
	acmd[4] = (lba >> 8) & 0xFF;
	acmd[5] = (lba >> 0) & 0xFF;

	if (opcode == 0xA8 || opcode == 0xAA) {
		// READ(12) / WRITE(12) - Transfer Length is 4 bytes at [6..9]
		acmd[6] = (sector_count >> 24) & 0xFF;
		acmd[7] = (sector_count >> 16) & 0xFF;
		acmd[8] = (sector_count >> 8) & 0xFF;
		acmd[9] = (sector_count >> 0) & 0xFF;
		acmd[10] = 0;
		acmd[11] = 0;
	} else {
		// READ(10) / WRITE(10) - Transfer Length is 2 bytes at [7..8]
		acmd[6] = 0;
		acmd[7] = (sector_count >> 8) & 0xFF;
		acmd[8] = (sector_count >> 0) & 0xFF;
		acmd[9] = 0;
		acmd[10] = 0;
		acmd[11] = 0;
	}
}

void ATAPIModule::probe(struct ioforge_block_device* block) { identify(block); }

void ATAPIModule::read_sector_size(struct ioforge_block_device* block) {
	uintptr_t resp_phys = 0;

	read_capacity10_resp_t* resp =
	    (read_capacity10_resp_t*)IOUtils::DMAAlloc(sizeof(*resp),
	                                               &resp_phys);

	memset(resp, 0, sizeof(*resp));

	uint8_t packet[12] = {0x25};

	struct ioforge_block_request cap_req = {
	    .op = IOFORGE_BLOCK_OP_PACKET,
	    .lba = 0,
	    .buffer = (void*)resp_phys,
	    .buffer_size = sizeof(*resp),
	    .block_count = 1,
	    .flags = IOFORGE_FLAG_DMA,
	    .packet_cmd = packet,
	    .packet_cmd_len = 12,
	    .timeout_ms = 5000,
	};

	if (!block->ops.submit(block, &cap_req)) {
		log(mod, "failed to idenitify device");
	}

	block->sector_size = __builtin_bswap32(resp->block_size);
	log(mod, "sector size %d", block->sector_size);
	IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
}

void ATAPIModule::identify(struct ioforge_block_device* block) {
	uintptr_t buff_phys = 0;
	uint16_t* buff = (uint16_t*)IOUtils::DMAAlloc(512, &buff_phys);
	memset(buff, 0, 512);

	struct ioforge_block_request req = {
	    .op = IOFORGE_BLOCK_OP_IDENTIFY,
	    .lba = 0,
	    .buffer = (void*)buff_phys,
	    .buffer_size = 512,
	    .block_count = 1,
	    .flags = IOFORGE_FLAG_DMA,
	    .packet_cmd = 0,
	    .packet_cmd_len = 10,
	    .timeout_ms = 5000,
	};

	if (!block->ops.submit(block, &req)) {
		log(mod, "failed to idenitify device");
		IOUtils::DMAFree((void*)buff_phys, (void*)buff, 512);
		return;
	}

	uint16_t info = buff[0];
	if (info & (1 << 15)) {
		log(mod, "ATAPI Device");
	}
	uint8_t method = (info & (0b1100000)) >> 5;
	log(mod, "method %b", method);

	uint16_t command_set_supported = buff[83] & 0b111;
	log(mod, "supported : %b", command_set_supported);

	char serial_number[21] = {0};
	read_ascii(serial_number, 10, buff, 20);
	log(mod, "serial number: %s", (char*)serial_number);

	char firmware_version[9] = {0};
	read_ascii(firmware_version, 23, buff, 8);
	log(mod, "firmware version: %s", (char*)firmware_version);

	char* model_number = (char*)kalloc(41);
	memset(model_number, 0, 41);
	read_ascii(model_number, 27, buff, 40);
	log(mod, "model number: %s", (char*)model_number);
	block->model_number = str(model_number);
	kfree2(model_number);

	// setup node
	uint16_t cmd_f_supported = buff[82];
	log(mod, "cmd_f_supported: %b", cmd_f_supported);

	uint16_t ata_version = (buff[80] & 0x1F) >> 1;
	log(mod, "ata_version support: %b", ata_version);

	log(mod, "block 0x%x", block);

	// read capacity
	read_sector_size(block);

	{
		// TODO: create this path dynamically cd0
		dentry_ptr dentry = 0;
		vxnamei("/dev/cd0", &dentry);
		auto vnode = create_and_attach_vnode();
		dentry->vnode = vnode;
		vnode->type = VNODE_TYPE_BLK;
		vnode->permission = 666;
		serial2_printf("dentry namei done\n");

		vops_blk_t* vops = (vops_blk_t*)kalloc(sizeof(vops_blk_t));
		vops->read = ATAPIModule::read;
		vops->write = ATAPIModule::write;
		vops->v_data = block;
		vnode->ops = (void*)vops;

		auto cdev = create_dev(vops, DEV_MAJOR_CDROM);
		if (!cdev)
			return;
		vnode->device.major = cdev->major;
		vnode->device.minor = cdev->minor;
		// vops->v_data = vnode;

		notify_call((char*)"/vfs/block", VFS_NOTIFY_PROBE,
		            (void*)vnode);
	}
	log(mod, "block 0x%x", block);

	IOUtils::DMAFree((void*)buff_phys, (void*)buff, 512);
}

extern "C" int ATAPIModule::read(void* vdata, uintptr_t addr, void* buf,
                                 size_t count) {
	auto i = ATAPIModule::getInstance();

	struct ioforge_block_device* block =
	    (struct ioforge_block_device*)vdata;
	if (!block) {
		log(i->mod, "read: block device is null");
		return -EINVAL;
	}

	if (!buf || count == 0) {
		log(i->mod, "read: invalid buffer or count");
		return -EINVAL;
	}

	const size_t sector_size =
	    block->sector_size ? block->sector_size : 2048;
	// uint32_t lba = (uint32_t)(addr / sector_size);
	uint32_t lba = (uint32_t)addr;
	uint16_t sector_count =
	    (uint16_t)((count + sector_size - 1) / sector_size);

	uintptr_t buff_phys;
	void* buff_ = IOUtils::DMAAlloc(sector_count * sector_size, &buff_phys);
	if (!buff_) {
		log(i->mod, "read: DMA alloc failed");
		return -2;
	}
	memset(buff_, 0, sector_count * sector_size);

	uint8_t acmd[16] = {0};
	i->build_acmd(0xA8, lba, sector_count, acmd);

	struct ioforge_block_request req = {
	    .op = IOFORGE_BLOCK_OP_PACKET,
	    .lba = lba,
	    .buffer = (void*)buff_phys,
	    .buffer_size = sector_count * sector_size,
	    .block_count = sector_count,
	    .flags = IOFORGE_FLAG_DMA,
	    .packet_cmd = acmd,
	    .packet_cmd_len = 12,
	    .timeout_ms = 5000,
	};

	int ret = block->ops.submit(block, &req);
	if (!ret) {
		log(i->mod, "read: submit failed (lba=%d, sectors=%d)", lba,
		    sector_count);
		IOUtils::DMAFree((void*)buff_phys, buff_,
		                 sector_count * sector_size);
		return -1;
	}

	memcopy(buf, buff_, count);

	IOUtils::DMAFree((void*)buff_phys, buff_, sector_count * sector_size);

	log(i->mod, "read: success (lba=%d, sectors=%d)", lba, sector_count);
	return (int)count;
}

extern "C" int ATAPIModule::write(void* vdata, uintptr_t addr, void* buf,
                                  size_t count) {
	auto i = ATAPIModule::getInstance();

	struct ioforge_block_device* block =
	    (struct ioforge_block_device*)vdata;
	if (!block) {
		log(i->mod, "write: block device is null");
		return -EINVAL;
	}

	if (!buf || count == 0) {
		log(i->mod, "write: invalid buffer or count");
		return -EINVAL;
	}

	// Cek apakah device support write
	if (!block->ops.submit) {
		log(i->mod,
		    "write: device is read-only or write not supported");
		return -EROFS;
	}

	const size_t sector_size =
	    block->sector_size ? block->sector_size : 2048;
	uint32_t lba = (uint32_t)(addr / sector_size);
	uint16_t sector_count =
	    (uint16_t)((count + sector_size - 1) / sector_size);

	uintptr_t buff_phys =
	    (uintptr_t)buf; // ganti virt_to_phys(buf) jika perlu
	uint8_t acmd[16] = {0};
	i->build_acmd(0xAA, lba, sector_count, acmd);

	struct ioforge_block_request req = {
	    .op = IOFORGE_BLOCK_OP_PACKET,
	    .lba = lba,
	    .buffer = (void*)buff_phys,
	    .buffer_size = sector_count * sector_size,
	    .block_count = sector_count,
	    .flags = IOFORGE_FLAG_DMA,
	    .packet_cmd = acmd,
	    .packet_cmd_len = 12,
	    .timeout_ms = 30000,
	};

	int ret = block->ops.submit(block, &req);
	if (ret) {
		log(i->mod, "write: submit failed (lba=%u, count=%u, err=%d)",
		    lba, sector_count, ret);
		return ret;
	}

	log(i->mod, "write: success (lba=%u, sectors=%u)", lba, sector_count);
	return 0;
}

void ATAPIModule::read_ascii(char* out, uint16_t off, uint16_t* buff,
                             uint16_t len) {
	memcopy(out, &buff[off], len);

	for (int i = 0; i < len; i += 2) {
		char temp = out[i];
		out[i] = out[i + 1];
		out[i + 1] = temp;
	}

	for (int i = len - 1; i >= 0; i--) {
		if (out[i] == ' ') {
			out[i] = '\0';
		} else {
			break;
		}
	}
}
