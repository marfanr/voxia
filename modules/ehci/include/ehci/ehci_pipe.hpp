#ifndef __EHCI__EHCI_PIPE_HPP__
#define __EHCI__EHCI_PIPE_HPP__

#include "ehci/ehci.hpp"
#include <ioforge/ioforge_int_pipe.hpp>

class EHCIPipe : public USBInterruptPipe {
      public:
	explicit EHCIPipe(EHCIModule* ehci)
	    : qh_node_(nullptr), data_node_(nullptr), ehci_(ehci), cb_(nullptr),
	      response_(0), response_buf_(0), toggle_(0), buf_(nullptr),
	      buf_phys_(0) {
	}

	bool
	open(const USBInterruptPipeDesc& desc, InterruptCallback cb) override;
	void close() override;
	void on_complete(uint8_t* buf, size_t len, bool error) override;

	ehci_queue_head_node_t* qh_node_;
	ehci_queue_task_descriptor_node_t* data_node_;
	USBInterruptPipeDesc desc_;

      private:
	EHCIModule* ehci_;
	InterruptCallback cb_;

	uintptr_t response_;
	uint32_t response_buf_;
	uint32_t toggle_;
	uint8_t* buf_;
	uintptr_t buf_phys_;

	void setup_qh();
	void arm();
};

#endif // __EHCI__EHCI_PIPE_HPP__