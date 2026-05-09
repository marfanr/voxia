#include <cstdint>
#include <ioforge/ioforge.hpp>
#include <ehci/ehci_pipe.hpp>

bool EHCIPipe::open(const InterruptPipeDesc& desc, InterruptCallback cb) {
	desc_ = desc;
	cb_ = cb;
	toggle_ = 0;

	ehci_->retrieve_qh(&qh_node_);
	IOForge::IOUtils::memset(qh_node_->head, 0,
				 sizeof(struct ehci_queue_head));

	ehci_->retrieve_qtd(&data_node_);
	IOForge::IOUtils::memset(data_node_->task_descriptor, 0,
				 sizeof(struct ehci_queue_task_descriptor));

	buf_ = (uint8_t*) IOForge::IOUtils::DMAAlloc(desc.buffer_size,
						     &buf_phys_);

	setup_qh();
	arm();
	ehci_->insert_periodic(qh_node_, desc.interval_ms);

	return true;
}

void EHCIPipe::setup_qh() {
	auto qh = qh_node_->head;
	qh->altTD = EHCI_QTD_TERMINATE;
	qh->currentTD = 0;
	qh->ch = (desc_.dev_addr & 0x7F);
	qh->ch |= ((desc_.endpoint & 0xF) << 8);
	qh->ch |= (desc_.speed & 0x3) << 12;	  // EPS High Speed
	qh->ch |= (desc_.endpoint & 0x7FF) << 16; // Max Packet
	qh->cap = (1 << 0);			  // S-mask
	qh->cap |= EHCI_QH_CAP_MULT_1;		  // ← FIX 2
}

void EHCIPipe::arm() {
	auto* qtd = data_node_->task_descriptor;
	IOForge::IOUtils::memset(qtd, 0, sizeof(*qtd));
	qtd->link = EHCI_QTD_TERMINATE;
	qtd->altlink = EHCI_QTD_TERMINATE;
	qtd->token = EHCI_QTD_TOKEN_LENGTH(desc_.buffer_size)
		     | EHCI_QTD_TOKEN_STATUS_ACTIVE | toggle_
		     | EHCI_QTD_TOKEN_PID_IN | EHCI_QTD_TOKEN_ERROR_COUNT_3
		     | EHCI_QTD_TOKEN_IOC;
	qtd->buffer[0] = (uint32_t) buf_phys_;

	toggle_ ^= EHCI_QTD_TOKEN_DATA;

	auto* qh = qh_node_->head;
	qh->token = 0;
	__sync_synchronize();
	qh->nextTD = data_node_->physaddr;
}

void EHCIPipe::close() {
	// ehci_->remove_periodic(qh_node_);
	// ehci_->release_qh(qh_node_);
	// ehci_->release_qtd(qtd_node_);
	// IOUtils::DMAFree(buf_);
}

void EHCIPipe::on_complete(uint8_t* /*buf*/, size_t len, bool error) {
	size_t real_len =
		desc_.buffer_size
		- ((data_node_->task_descriptor->token >> 16) & 0x7FFF);
	if (!error && cb_)
		cb_(buf_, real_len); // naik ke USBDevice::handle_report
	arm();
}
