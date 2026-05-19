#ifndef __IOFORGE__IOFORGE_BLOCK_HPP__
#define __IOFORGE__IOFORGE_BLOCK_HPP__

#include "block/block.h"
#include "ioforge/ioforge.hpp"
#include "ioforge/ioforge_block.h"

class IOForgeBlock : public IOForge {
      public:
	inline IOForgeBlock(const char* mod) : IOForge(mod) {}
	static void create(const char* name, block_device_operations_t* ops,
	                   void* identifier);

	template <typename T>
	void foreach_by_type(ioforge_device* node, uint8_t type, T&& callback) {
		foreach_block_device_by_type(
		    node, type,
		    [](ioforge_block_device* dev, void* ctx) {
			    (*reinterpret_cast<T*>(ctx))(dev);
		    },
		    &callback);
	}

	virtual void load() = 0;
	virtual void unload() = 0;
};

#endif // __IOFORGE__IOFORGE_BLOCK_HPP__