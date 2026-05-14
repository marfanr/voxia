#include "ioforge/ioforge_block.h"
#include <atapi/atapi.hpp>
#include <cstdint>
#include <str.h>

void ATAPIModule::probe(struct ioforge_block_device* block) {
	uintptr_t buff_phys = 0;
	uint8_t* buff = (uint8_t*) IOUtils::DMAAlloc(512, &buff_phys);
	memset(buff, 0, 512);

	uint32_t lba = 0;
	uint32_t sector_count = 1;

	uint8_t acmd[16] = {0};
	acmd[0] = 0xA1;
	acmd[1] = 0;
	acmd[2] = (lba >> 24) & 0xFF;
	acmd[3] = (lba >> 16) & 0xFF;
	acmd[4] = (lba >> 8) & 0xFF;
	acmd[5] = (lba >> 0) & 0xFF;
	acmd[6] = 0;
	acmd[7] = (sector_count >> 8) & 0xFF;
	acmd[8] = (sector_count >> 0) & 0xFF;
	acmd[9] = 0;

	log(mod, "acmd at 0x%x", acmd);

	struct ioforge_block_request req = {
		.op = IOFORGE_BLOCK_OP_IDENTIFY,
		.lba = 0,
		.buffer = (void*) buff_phys,
		.buffer_size = 512,
		.block_count = sector_count,
		.flags = 0,
		.packet_cmd = acmd,
		.packet_cmd_len = 10,
		.timeout_ms = 30000,
	};
	if (block->ops.submit(block, &req)) {
		log(mod, "success");
		for (int i = 0; i < 100; i++) {
			serial2_printf("%x ", buff[i]);
			if ((i + 1) % 16 == 0) {
				serial2_printf("\n");
			}
		}
		serial2_printf("\n");
	}
}