#include "xhci/xhci_pipe.hpp"
#include <ioforge/ioforge.hpp>

bool XHCIPipe::open(const USBInterruptPipeDesc& desc, InterruptCallback cb) {
    desc_ = desc;
    cb_ = cb;

    buf_ = (uint8_t*)IOForge::IOUtils::DMAAlloc(desc.buffer_size, &buf_phys_);

    uint8_t ep_num = desc.endpoint & 0xF;
    uint8_t full_ep = ep_num | 0x80;

    bool success = xhci_->configure_endpoint(desc.dev_addr, full_ep, 3, desc.buffer_size, desc.interval_ms);
    if (!success) {
        serial2_printf("[XHCI] ERROR: Failed to configure endpoint %d for slot %d\n", full_ep, desc.dev_addr);
        return false;
    }

    uint8_t is_in = 1;
    uint32_t dci = (ep_num * 2) + is_in;
    ep_idx_ = dci - 1;

    arm();
    return true;
}

void XHCIPipe::arm() {
    xhci_->queue_interrupt_transfer(desc_.dev_addr, ep_idx_, buf_phys_, desc_.buffer_size);
}

void XHCIPipe::close() {
    IOForge::IOUtils::DMAFree((void *)buf_phys_, buf_, desc_.buffer_size);
}

void XHCIPipe::on_complete(uint8_t* /*buf*/, size_t len, bool error) {
    if (!error && cb_) {
        cb_(buf_, len);
        arm();
    }
}
