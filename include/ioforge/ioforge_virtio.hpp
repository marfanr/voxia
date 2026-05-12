#ifndef __IOFORGE__IOFORGE_VIRTIO_HPP_
#define __IOFORGE__IOFORGE_VIRTIO_HPP_

#include "ioforge/ioforge.hpp"

class IoForgeVirtio : public IOForge {
      public:
	IoForgeVirtio(const char* mod) : IOForge(mod) {
	}
	virtual void load();
	virtual void unload();
};

#endif // __IOFORGE__IOFORGE_VIRTIO_HPP_