#include "xhci/xhci_pipe.hpp"
#include <ioforge/ioforge.hpp>

bool XHCIPipe::open(const USBInterruptPipeDesc& desc, InterruptCallback cb) {
    desc_ = desc;
    cb_ = cb;

    buf_ = (uint8_t*)IOForge::IOUtils::DMAAlloc(desc.buffer_size, &buf_phys_);

    // Configure the endpoint in XHCI
    // ep_type = 3 for Interrupt Endpoint
    bool success = xhci_->configure_endpoint(desc.dev_addr, desc.endpoint, 3, desc.buffer_size, desc.interval_ms);
    if (!success) {
        serial2_printf("[XHCI] ERROR: Failed to configure endpoint %d for slot %d\n", desc.endpoint, desc.dev_addr);
        return false;
    }

    uint8_t ep_num = desc.endpoint & 0xF;
    uint8_t is_in = (desc.endpoint & 0x80) ? 1 : 0;
    uint32_t dci = (ep_num * 2) + is_in;
    ep_idx_ = dci - 1;

    arm();
    return true;
}

void XHCIPipe::arm() {
    xhci_->queue_interrupt_transfer(desc_.dev_addr, ep_idx_, buf_phys_, desc_.buffer_size);
}

void XHCIPipe::close() {
    // IOUtils::DMAFree(buf_);
}

void XHCIPipe::on_complete(uint8_t* /*buf*/, size_t len, bool error) {
    // serial2_printf("[XHCI] XHCIPipe: on_complete len=%d error=%d\n", len, error);
    if (!error && cb_) {
        cb_(buf_, len);
        arm(); // Only re-arm if successful
    }
}
