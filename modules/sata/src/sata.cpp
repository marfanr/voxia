#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_block.h"
#include "notify.h"
#include "vfs/vfs.h"
#include "vfs/vnode.h"
#include <sata/sata.hpp>
#include <str.h>

typedef struct {
	uint32_t last_lba;
	uint32_t block_size;
} __attribute__((packed)) read_capacity10_resp_t;

void SATAModule::probe(struct ioforge_block_device* block) { identify(block); }

void SATAModule::read_sector_size(struct ioforge_block_device* block) {
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

void SATAModule::identify(struct ioforge_block_device* block) {
	uintptr_t buff_phys = 0;
	uint16_t* buff = (uint16_t*)IOUtils::DMAAlloc(512, &buff_phys);
	memset(buff, 0, 512);

	struct ioforge_block_request req = {
	    .op = IOFORGE_BLOCK_OP_IDENTIFY,
	    .lba = 0,
	    .buffer = (void*)buff_phys,
	    .buffer_size = 512,
	    .block_count = 1,
	    .flags = 0,
	    .packet_cmd = 0,
	    .packet_cmd_len = 10,
	    .timeout_ms = 5000,
	};

	if (!block->ops.submit(block, &req)) {
		log(mod, "failed to idenitify device");
		IOUtils::DMAFree((void*)buff_phys, (void*)buff, 512);
		return;
	}

	// refer Table 45 ATA/ATAPI Command Set
	// TODO: save to dev or block
	char serial_number[21] = {0};
	read_ascii(serial_number, 10, buff, 20);
	log(mod, "serial number : %s\n", serial_number);

	char firmware_version[9] = {0};
	read_ascii(firmware_version, 23, buff, 8);
	log(mod, "firmware version: %s", (char*)firmware_version);

	char* model_number = (char*)kalloc(41);
	memset(model_number, 0, 41);
	read_ascii(model_number, 27, buff, 40);
	log(mod, "model number: %s", (char*)model_number);

	block->sector_size = 512;

	{
		// TODO: create this path dynamically cd0
		dentry_ptr dentry = 0;

		// TODO: alloc dynamically from kernel , if has more than 1 device
		vxnamei("/dev/hd0", &dentry);
		auto vnode = create_and_attach_vnode();
		dentry->vnode = vnode;
		vnode->type = VNODE_TYPE_BLK;
		vnode->permission = 666;
		serial2_printf("dentry namei done\n");

		vops_blk_t* vops = (vops_blk_t*)kalloc(sizeof(vops_blk_t));
		vops->open = 0;
		vops->read = SATAModule::read;
		vops->write = SATAModule::write;
		vops->flush = SATAModule::flush;
		vops->v_data = block;
		vnode->ops = (void*)vops;

		auto cdev = create_dev(vops, DEV_MAJOR_HD);
		if (!cdev)
			return;
		vnode->device.major = cdev->major;
		vnode->device.minor = cdev->minor;

		notify_call((char*)"/vfs/block", VFS_NOTIFY_PROBE,
		            (void*)dentry);
	}
	log(mod, "block 0x%x", block);

	IOUtils::DMAFree((void*)buff_phys, (void*)buff, 512);
}

extern "C" int SATAModule::read(vnode_t* vnode, uintptr_t addr, void* buf,
                                size_t count) {
	auto i = SATAModule::getInstance();

	auto vops = (vops_blk_t*)vnode->ops;
	if (!vops) {
		log(i->mod, "read: vops is null");
		return -EINVAL;
	}

	struct ioforge_block_device* block =
	    (struct ioforge_block_device*)vops->v_data;
	if (!block) {
		log(i->mod, "read: block device is null");
		return -EINVAL;
	}

	if (!buf || count == 0) {
		log(i->mod, "read: invalid buffer or count");
		return -EINVAL;
	}

	const size_t sector_size =
	    block->sector_size ? block->sector_size : 512;
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

	struct ioforge_block_request req = {
	    .op = IOFORGE_BLOCK_OP_READ,
	    .lba = lba,
	    .buffer = (void*)buff_phys,
	    .buffer_size = sector_count * sector_size,
	    .block_count = sector_count,
	    .flags = IOFORGE_FLAG_DMA,
	    .packet_cmd = 0,
	    .packet_cmd_len = 0,
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

extern "C" int SATAModule::write(vnode_t* vnode, uintptr_t addr, void* buf,
                                 size_t count) {
	auto i = SATAModule::getInstance();
	auto vops = (vops_blk_t*)vnode->ops;
	if (!vops) {
		log(i->mod, "write: vops is null");
		return -EINVAL;
	}

	struct ioforge_block_device* block =
	    (struct ioforge_block_device*)vops->v_data;
	if (!block) {
		log(i->mod, "write: block device is null");
		return -EINVAL;
	}

	if (!buf || count == 0) {
		log(i->mod, "write: invalid buffer or count");
		return -EINVAL;
	}

	if (!block->ops.submit) {
		log(i->mod,
		    "write: device is read-only or write not supported");
		return -EROFS;
	}

	const size_t sector_size =
	    block->sector_size ? block->sector_size : 512;
	uint32_t lba = (uint32_t)addr;
	uint16_t sector_count =
	    (uint16_t)((count + sector_size - 1) / sector_size);

	uintptr_t buff_phys;
	void* buff_ = IOUtils::DMAAlloc(sector_count * sector_size, &buff_phys);
	if (!buff_) {
		log(i->mod, "write: DMA alloc failed");
		return -2;
	}
	memcopy(buff_, buf, count);

	struct ioforge_block_request req = {
	    .op = IOFORGE_BLOCK_OP_WRITE,
	    .lba = lba,
	    .buffer = (void*)buff_phys,
	    .buffer_size = sector_count * sector_size,
	    .block_count = sector_count,
	    .flags = IOFORGE_FLAG_DMA,
	    .packet_cmd = 0,
	    .packet_cmd_len = 0,
	    .timeout_ms = 30000,
	};

	int ret = block->ops.submit(block, &req);
	if (!ret) {
		log(i->mod, "write: submit failed (lba=%u, count=%u)",
		    lba, sector_count);
		IOUtils::DMAFree((void*)buff_phys, buff_, sector_count * sector_size);
		return -1;
	}

	IOUtils::DMAFree((void*)buff_phys, buff_, sector_count * sector_size);
	log(i->mod, "write: success (lba=%u, sectors=%u)", lba, sector_count);
	return (int)count;
}

extern "C" int SATAModule::flush(vnode_t* vnode) {
	auto vops = (vops_blk_t*)vnode->ops;
	if (!vops) {
		return -EINVAL;
	}

	struct ioforge_block_device* block =
	    (struct ioforge_block_device*)vops->v_data;
	if (!block || !block->ops.submit) {
		return -EINVAL;
	}

	struct ioforge_block_request req = {
	    .op = IOFORGE_BLOCK_OP_FLUSH,
	    .lba = 0,
	    .buffer = 0,
	    .buffer_size = 0,
	    .block_count = 0,
	    .flags = 0,
	    .packet_cmd = 0,
	    .packet_cmd_len = 0,
	    .timeout_ms = 30000,
	};
	return block->ops.submit(block, &req) ? 0 : -1;
}

void SATAModule::read_ascii(char* out, uint16_t off, uint16_t* buff,
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