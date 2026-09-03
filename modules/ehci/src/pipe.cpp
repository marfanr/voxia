#include "ehci/ehci.hpp"
#include <cstdint>
#include <ehci/ehci_pipe.hpp>
#include <ioforge/ioforge.hpp>

bool EHCIPipe::open(const USBInterruptPipeDesc& desc, InterruptCallback cb) {
	desc_ = desc;
	cb_ = cb;
	toggle_ = 0;

	if (!ehci_->retrieve_qh(&qh_node_)) {
		serial2_printf("[open] ERROR: retrieve_qh failed\n");
		return false;
	}
	if (!ehci_->retrieve_qtd(&data_node_)) {
		serial2_printf("[open] ERROR: retrieve_qtd failed\n");
		return false;
	}

	serial2_printf("qh node new 0x%lx\n", qh_node_->physaddr);

	IOForge::IOUtils::memset(qh_node_->head, 0,
	                         sizeof(struct ehci_queue_head));

	// serial2_printf("data node before 0x%lx\n", data_node_);
	
	serial2_printf("data node new 0x%lx\n", data_node_->physaddr);
	IOForge::IOUtils::memset(data_node_->task_descriptor, 0,
	                         sizeof(struct ehci_queue_task_descriptor));

	buf_ =
	    (uint8_t*)IOForge::IOUtils::DMAAlloc(desc.buffer_size, &buf_phys_);

	setup_qh();
	arm();
	ehci_->insert_periodic(qh_node_, desc.interval_ms);

	return true;
}

void EHCIPipe::setup_qh() {
	auto qh = qh_node_->head;
	qh->qhlp = qh_node_->physaddr | EHCI_Q_SELECT_QH;
	qh->altTD = EHCI_QTD_TERMINATE;
	qh->currentTD = 0;
	qh->ch = (desc_.dev_addr & 0x7F);
	qh->ch |= ((desc_.endpoint & 0xF) << 8);
	qh->ch |= (2) << 12;
	qh->ch |= ((64 & 0x7FF) << 16);
	; // Max Packet
	qh->ch |= EHCI_QH_CAP_DTC;
	qh->cap = (1 << 0)              // S-mask bit 0 = microframe 0
	          | EHCI_QH_CAP_MULT_1; // Mult = 1 (bits 30:29)

	serial2_printf("[V3] EHCIPipe: QH setup addr=%d ep=%d speed=%d\n",
	               desc_.dev_addr, desc_.endpoint, desc_.speed);
}

void EHCIPipe::arm() {
	auto* qtd = data_node_->task_descriptor;
	IOForge::IOUtils::memset(qtd, 0, sizeof(*qtd));
	qtd->link = EHCI_QTD_TERMINATE;
	qtd->altlink = EHCI_QTD_TERMINATE;
	qtd->token = EHCI_QTD_TOKEN_LENGTH(desc_.buffer_size) |
	             EHCI_QTD_TOKEN_STATUS_ACTIVE | toggle_ |
	             EHCI_QTD_TOKEN_PID_IN | EHCI_QTD_TOKEN_ERROR_COUNT_3 |
	             EHCI_QTD_TOKEN_IOC;
	qtd->buffer[0] = (uint32_t)buf_phys_;

	auto* qh = qh_node_->head;
	// JANGAN set qh->token manual
	// Cukup reset overlay dan tunjuk ke QTD baru:
	qh->currentTD = 0;
	qh->nextTD = data_node_->physaddr;
	qh->altTD = EHCI_QTD_TERMINATE;
	// Bersihkan overlay token supaya HC tidak bingung:
	qh->token = 0; // <-- 0, bukan ACTIVE
	__sync_synchronize();
}

void EHCIPipe::on_complete(uint8_t* /*buf*/, size_t len, bool error) {
	uint32_t token = data_node_->task_descriptor->token;
	size_t real_len = desc_.buffer_size - ((token >> 16) & 0x7FFF);

	if (!error && cb_) {
		// Copy dulu ke stack/temp buffer sebelum re-arm
		uint8_t tmp[64];
		IOForge::IOUtils::memcpy(tmp, buf_, real_len);
		toggle_ ^= EHCI_QTD_TOKEN_DATA;
		arm();              // re-arm segera supaya HC tidak idle
		cb_(tmp, real_len); // callback pakai copy
	} else {
		arm(); // re-arm meski error, supaya pipe tidak mati
	}
}