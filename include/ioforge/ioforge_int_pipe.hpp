#ifndef __IOFORGE__IOFORGE_INT_PIPE_HPP__
#define __IOFORGE__IOFORGE_INT_PIPE_HPP__

#include <type.h>

using InterruptCallback = void (*)(const uint8_t* data, size_t len);

struct InterruptPipeDesc {
	uint8_t dev_addr;
	uint8_t endpoint;
	uint8_t speed;	      // 0=Full, 1=Low, 2=High/Super
	uint16_t interval_ms; // polling interval
	size_t buffer_size;
};

class InterruptPipe {
      public:
	virtual ~InterruptPipe() = default;

	// Mulai polling — callback dipanggil tiap ada data
	virtual bool
	open(const InterruptPipeDesc& desc, InterruptCallback cb) = 0;

	// Hentikan polling dan bebaskan resource
	virtual void close() = 0;

	// // Dipanggil dari interrupt handler controller
	virtual void on_complete(uint8_t* buf, size_t len, bool error) = 0;
};

#endif // __IOFORGE__IOFORGE_INT_PIPE_HPP__