#ifndef __XHCI__XHCI_PIPE_HPP__
#define __XHCI__XHCI_PIPE_HPP__

#include "xhci/xhci.hpp"
#include <ioforge/ioforge_int_pipe.hpp>

class XHCIPipe : public USBInterruptPipe {
public:
    explicit XHCIPipe(XHCIModule* xhci)
        : ep_idx_(0), xhci_(xhci), cb_(nullptr), buf_(nullptr), buf_phys_(0) {}

    bool open(const USBInterruptPipeDesc& desc, InterruptCallback cb) override;
    void close() override;
    void on_complete(uint8_t* buf, size_t len, bool error) override;

    USBInterruptPipeDesc desc_;
    uint32_t ep_idx_;

private:
    XHCIModule* xhci_;
    InterruptCallback cb_;
    uint8_t* buf_;
    uintptr_t buf_phys_;

    void arm();
};

#endif // __XHCI__XHCI_PIPE_HPP__
