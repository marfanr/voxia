#ifndef __IOFORGE__IOFORGE_BLOCK_H__
#define __IOFORGE__IOFORGE_BLOCK_H__

#include "string.h"
#include <type.h>
#include <ioforge/ioforge.h>

#ifdef __cplusplus
extern "C" {
#endif

enum ioforge_block_op_type : uint16_t {
	IOFORGE_BLOCK_OP_READ = 0,   // ATA: READ DMA EXT
	IOFORGE_BLOCK_OP_WRITE = 1,  // ATA: WRITE DMA EXT
	IOFORGE_BLOCK_OP_PACKET = 2, // ATAPI: PACKET command (0xA0)
	IOFORGE_BLOCK_OP_FLUSH = 3,  // ATA: FLUSH CACHE EXT
	IOFORGE_BLOCK_OP_IDENTIFY = 4,
};

// Flags untuk ATAPI
enum ioforge_block_flags : uint32_t {
	IOFORGE_FLAG_DMA = (1 << 0), // DMA mode (vs PIO)
	IOFORGE_FLAG_WRITE =
		(1 << 1), // Direction: device → host jika 0 (for DMA)
	IOFORGE_FLAG_NODATA = (1 << 2), // Packet cmd tanpa data transfer
};

struct ioforge_block_request {
	uint16_t op;

	uint64_t lba;

	void* buffer;
	size_t buffer_size;

	uint32_t block_count;

	uint32_t flags;

	void* packet_cmd;
	size_t packet_cmd_len;

	uint32_t timeout_ms;
};

struct ioforge_block_device;

struct ioforge_block_op {
	int (*submit)(struct ioforge_block_device* dev,
		      struct ioforge_block_request* req);
};

enum {
	IOFORGE_BLOCK_TYPE_SATA = 0,
	IOFORGE_BLOCK_TYPE_SATAPI,
	IOFORGE_BLOCK_TYPE_SEMB,
	IOFORGE_BLOCK_TYPE_PM
};

struct ioforge_block_device {
	struct ioforge_device base;
	struct ioforge_block_op ops;
	uint8_t port;
	uint8_t type;
	size_t sector_size;
	kstring model_number;
};

typedef void (*ioforge_block_visitor_fn)(struct ioforge_block_device* dev,
					 void* ctx);

void ioforge_find_block_device_by_type(struct ioforge_device* node,
				       uint8_t type,
				       ioforge_block_visitor_fn callback,
				       void* ctx);

#define EINVAL 22
#define EROFS 20

#ifdef __cplusplus
}
#endif

#endif // __IOFORGE__IOFORGE_BLOCK_H__