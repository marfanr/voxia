#ifndef __SATA__SATA_HPP__
#define __SATA__SATA_HPP__

#include <ioforge/ioforge_block.h>
#include <ioforge/ioforge_block.hpp>
#include <vfs/filesystem.h>

class SATAModule : public IOForgeBlock {
      public:
	SATAModule();
	void load() override;
	void unload() override;

	static SATAModule* getInstance();
	static int read(vnode_t* vnode, uintptr_t addr, void* buf, size_t count);
	static int write(vnode_t* vnode, uintptr_t addr, void* buf, size_t count);
	static int flush(vnode_t* vnode);

      protected:
	void probe(struct ioforge_block_device* block);
	void identify(struct ioforge_block_device* block);
	void read_sector_size(struct ioforge_block_device* block);
	void build_acmd(uint8_t opcode, uint32_t lba, uint32_t sector_count,
			uint8_t (&acmd)[16]);

	void read_ascii(char* out, uint16_t off, uint16_t* buff, uint16_t len);

      private:
};

#endif //__SATA__SATA_HPP__