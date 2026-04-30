#include "ethernet.h"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.h"
#include "libk/serial.h"
#include "libk/str.h"
#include <memory/memory_utils.h>

// TODO: handle race condition jika dipanggil di scheduler
void ethernet_send_frame(struct ioforge_nic_service* nic,
			 struct netbuff* netbuff, uint16_t ethertype,
			 const uint8_t dst_mac[6]) {

	uint8_t self_mac[6];
	if (!nic->ops->get_mac_address(self_mac)) {
		LOG2_ERROR("Ethernet", "Failed to get MAC address");
		return;
	}
	struct ethernet_header* eth = (struct ethernet_header*) netbuff_push(
		netbuff, sizeof(struct ethernet_header));

	memcopy((void*) eth->dest_mac, (void*) dst_mac, 6);
	memcopy((void*) eth->src_mac, (void*) self_mac, 6);
	eth->ethertype = ethertype;

	if (netbuff->length < 60) {
		// Tambahkan nol di belakang agar paket pas 60 byte
		size_t pad_len = 60 - netbuff->length;
		void* pad_area = netbuff_put(netbuff, pad_len);
		memset(pad_area, 0, pad_len);
	}

	uintptr_t current_paddr =
		netbuff->paddr + (netbuff->data - netbuff->head);

	struct data_template data[1];
	data[0] = (struct data_template){.buffer = (void*) current_paddr,
					 .len = netbuff->length,
					 .wait_next_data = false};

	nic->ops->send(data, 1);
}