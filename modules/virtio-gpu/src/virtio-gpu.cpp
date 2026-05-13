#include "type.h"
#include <cstdint>
#include <stdint.h>
#include <virtio-gpu/virtio-gpu.hpp>
#include <str.h>

//TODO: refactor
//  Semua ini tidak ada hubungannya dengan GPU spesifik
// virtq_init()
// virtq_alloc_desc()
// virtq_free_desc()
// virtq_add_buf()
// virtq_kick()
// virtq_get_used_elem()

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

struct virtio_gpu_queue VirtioGpu::control_queue_ = {0};

static volatile boolean_t interrupt_exist = 0;

void VirtioGpu::setup() {
	uint8_t common_bar = dev_->common_cfg.bar;
	log(mod, "common config BAR: %d", common_bar);

	uintptr_t common_phys =
		dev_->pci.bar[common_bar].address + dev_->common_cfg.offset;
	log(mod, "common config phys addr: 0x%x", common_phys);

	volatile virtio_pci_common_cfg_t* common_cfg =
		(volatile virtio_pci_common_cfg_t*) common_phys;

	uintptr_t notify_phys =
		dev_->pci.bar[common_bar].address + dev_->notify_cfg.offset;
	notify_base_ = notify_phys;
	log(mod, "notify config phys addr: 0x%x", notify_phys);

	notify_multiplier_ = dev_->notify_cfg.length;

	// Acknowledge device
	uint8_t status = 0;
	common_cfg->device_status = status; // write: 0x0

	status |= VIRTIO_STATUS_ACKNOWLEDGE;
	common_cfg->device_status = status; // write: 0x1

	status |= VIRTIO_STATUS_DRIVER;
	common_cfg->device_status = status;

	// Feature negotiation
	status |= VIRTIO_STATUS_FEATURES_OK;
	common_cfg->device_status = status;

	// Verify features
	if (!(common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
		log(mod, "Feature negotiation failed");
		return;
	}

	auto device_features = common_cfg->device_feature;
	log(mod, "Device features: 0x%x", device_features);

	status |= VIRTIO_STATUS_DRIVER_OK;
	common_cfg->device_status = status;

	//

	// Feature negotiation
	common_cfg->device_feature_select = 0;

	// Accept only essential features for now
	common_cfg->driver_feature_select = 0;
	common_cfg->driver_feature =
		device_features & 0x1; // Only basic feature bit
	common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;

	// Verify features
	if (!(common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
		log(mod, "Feature negotiation failed");
		return;
	}
	log(mod, "Features OK");

	// Setup control queue
	common_cfg->queue_select = 0;
	uint16_t queue_size = common_cfg->queue_size;
	if (queue_size == 0 || queue_size > VIRTIO_GPU_QUEUE_SIZE) {
		queue_size = VIRTIO_GPU_QUEUE_SIZE;
	}
	log(mod, "Queue size: %d", queue_size);

	// Allocate and initialize virtqueue
	uintptr_t desc_size = queue_size * sizeof(struct virtq_desc);
	uintptr_t avail_off = desc_size;
	uintptr_t avail_size = sizeof(uint16_t) * (2 + queue_size);
	uintptr_t used_off =
		ALIGN_UP(avail_off + avail_size, VIRTIO_GPU_QUEUE_ALIGN);
	uintptr_t used_size = sizeof(struct virtq_used)
			      + sizeof(struct virtq_used_elem) * queue_size;
	size_t vq_total_size =
		ALIGN_UP(used_off + used_size, VIRTIO_GPU_QUEUE_ALIGN);

	uintptr_t vq_phys = 0;
	void* vq_mem = IOUtils::DMAAlloc(vq_total_size, &vq_phys);

	if (!vq_mem) {
		log(mod, "Failed to allocate virtqueue memory");
		return;
	}
	memset(vq_mem, 0, vq_total_size);

	virtq_init(&control_queue_, vq_mem, queue_size, vq_phys);

	// Configure queue with physical addresses
	common_cfg->queue_desc = vq_phys;
	common_cfg->queue_avail =
		vq_phys + queue_size * sizeof(struct virtq_desc);

	// Calculate used ring address properly
	uintptr_t used_offset =
		(uintptr_t) control_queue_.used - (uintptr_t) vq_mem;
	common_cfg->queue_used = vq_phys + used_offset;

	notify_offset_ = common_cfg->queue_notify_off * notify_multiplier_;

	log(mod,
	    "Virtqueue configured: desc=0x%x, avail=0x%x, used=0x%x, "
	    "notify_offset_=0x%x",
	    common_cfg->queue_desc, common_cfg->queue_avail,
	    common_cfg->queue_used, notify_offset_);

	// Enable queue
	common_cfg->queue_enable = 1;
	log(mod, "Queue enabled");

	// Driver OK
	common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
	log(mod, "Driver OK");

	// FIX: Add delay to ensure device is ready
	for (volatile int i = 0; i < 100000; i++)
		;

	// gpu_dev.initialized = true;

	log(mod, "VirtIO GPU initialized successfully");

	// irq
	if (dev_->pci.interrupt_line) {
		auto irq = IOUtils::irq_alloc_entry();
		IOUtils::isr_map(dev_->pci.interrupt_line, irq);
		IOUtils::irq_register(irq, (void*) VirtioGpu::fireHandler);
	}

	// -----
	// TEST
	// -----
	// Allocate buffers untuk test
	uintptr_t test_phys = 0;
	struct virtio_gpu_ctrl_hdr* test_cmd =
		(struct virtio_gpu_ctrl_hdr*) IOUtils::DMAAlloc(
			sizeof(*test_cmd), &test_phys);

	uintptr_t test_resp_phys = 0;
	struct virtio_gpu_resp_display_info* test_resp =
		(struct virtio_gpu_resp_display_info*) IOUtils::DMAAlloc(
			sizeof(*test_resp), &test_resp_phys);

	if (!test_cmd || !test_resp) {
		log(mod, "Failed to allocate test buffers");
		return;
	}

	// Test communication
	memset(test_cmd, 0, sizeof(*test_cmd));
	test_cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

	memset(test_resp, 0, sizeof(*test_resp));

	log(mod, "Test buffers allocated: cmd=0x%x, resp=0x%x", test_cmd,
	    test_resp);

	// Beri waktu untuk device stabil
	for (volatile int i = 0; i < 1000000; i++)
		;

	// Coba kirim command
	int result = virtio_gpu_send_command(
		(void*) test_phys, sizeof(*test_cmd), (void*) test_resp_phys,
		sizeof(*test_resp));

	if (result == 0) {
		log(mod, "GPU communication test PASSED");

		// print resp
		log(mod, "Response type: 0x%x len : %d", test_resp->hdr.type,
		    sizeof(*test_resp));

		if (test_resp->hdr.type == VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
			log(mod, "Display info received successfully");
			int enabled_count = 0;
			for (int i = 0; i < 16; i++) {
				if (test_resp->pmodes[i].enabled) {
					log(mod, "Scanout %d: %dx%d at (%d,%d)",
					    i, test_resp->pmodes[i].rect.width,
					    test_resp->pmodes[i].rect.height,
					    test_resp->pmodes[i].rect.x,
					    test_resp->pmodes[i].rect.y);
					enabled_count++;
				}
			}
			if (enabled_count == 0) {
				log(mod, "No enabled display modes found");
			}
		} else {
			log(mod, "Unexpected response type: 0x%x",
			    test_resp->hdr.type);
		}
	} else {
		log(mod, "GPU communication test FAILED");
	}

	// / Create a test resource
	if (virtio_gpu_create_resource(1, 1366, 768) == 0) {
		log(mod, "Test resource created successfully");
	} else {
		log(mod, "Failed to create test resource");
	}
}

void VirtioGpu::fireHandler() {
	auto instance = VirtioGpu::getInstance();
	uintptr_t isr_addr =
		instance->dev_->pci.bar[instance->dev_->isr_cfg.bar].address
		+ instance->dev_->isr_cfg.offset;

	uint8_t isr = *(uint8_t*) isr_addr;
	if (isr & (1 << 0)) {
		interrupt_exist = true;
	}
}

uint16_t VirtioGpu::virtq_alloc_desc(struct virtio_gpu_queue* vq) {
	if (vq->num_free == 0) {
		log(mod, "No free descriptors");
		return vq->queue_size;
	}

	uint16_t desc_idx = vq->free_head;
	vq->free_head = vq->desc[desc_idx].next;
	vq->num_free--;

	// Clear the descriptor
	vq->desc[desc_idx].addr = 0;
	vq->desc[desc_idx].len = 0;
	vq->desc[desc_idx].flags = 0;
	vq->desc[desc_idx].next = 0;

	return desc_idx;
}

void VirtioGpu::virtq_free_desc(struct virtio_gpu_queue* vq,
				uint16_t desc_idx) {
	vq->desc[desc_idx].next = vq->free_head;
	vq->free_head = desc_idx;
	vq->num_free++;
}

void VirtioGpu::virtq_init(struct virtio_gpu_queue* vq, void* vq_mem,
			   uint16_t queue_size, uintptr_t phys_addr) {
	uintptr_t base = (uintptr_t) vq_mem;

	vq->desc = (struct virtq_desc*) base;

	uintptr_t avail_off = queue_size * sizeof(struct virtq_desc);
	vq->avail = (struct virtq_avail*) (base + avail_off);

	// avail size = flags(2) + idx(2) + ring[queue_size](2 each) + used_event(2)
	uintptr_t used_off = avail_off + sizeof(uint16_t) * (3 + queue_size);
	used_off = ALIGN_UP(used_off, VIRTIO_GPU_QUEUE_ALIGN);
	vq->used = (struct virtq_used*) (base + used_off);

	vq->queue_size = queue_size;
	vq->free_head = 0;
	vq->num_free = queue_size;
	vq->last_used_idx = 0;
	vq->phys_addr = phys_addr;

	// Initialize descriptor chain
	for (uint16_t i = 0; i < queue_size - 1; i++) {
		vq->desc[i].next = i + 1;
	}
	vq->desc[queue_size - 1].next = 0;

	// Initialize rings
	memset(vq->avail, 0,
	       sizeof(struct virtq_avail) + sizeof(uint16_t) * queue_size);
	memset(vq->used, 0,
	       sizeof(struct virtq_used)
		       + sizeof(struct virtq_used_elem) * queue_size);

	vq->avail->idx = 0;
	vq->used->idx = 0;

	log(mod, "Virtqueue initialized: desc=%x, avail=%x, used=%x", vq->desc,
	    vq->avail, vq->used);
}

int VirtioGpu::virtq_add_buf(struct virtio_gpu_queue* vq, void** buffers,
			     uint32_t* lengths, uint16_t num_out,
			     uint16_t num_in, uint16_t* head_out) {

	if (num_out + num_in == 0 || num_out + num_in > vq->num_free) {
		log(mod, "Not enough descriptors: free=%d, needed=%d",
		    vq->num_free, num_out + num_in);
		return -1;
	}

	uint16_t head = virtq_alloc_desc(vq);
	uint16_t prev = head;
	*head_out = head;

	log(mod, "Building descriptor chain, head=%d, num_out=%d, num_in=%d",
	    head, num_out, num_in);

	// Add output buffers (device reads)
	for (uint16_t i = 0; i < num_out; i++) {
		// uint64_t phys_addr = virt_to_phys(buffers[i]);
		uint64_t phys_addr = (uint64_t) buffers[i];
		vq->desc[prev].addr = phys_addr;
		vq->desc[prev].len = lengths[i];
		vq->desc[prev].flags = VIRTQ_DESC_F_NEXT;

		log(mod, "OUT desc[%d]: phys=0x%x, len=%d, flags=0x%x, next=%d",
		    prev, phys_addr, lengths[i], vq->desc[prev].flags,
		    vq->desc[prev].next);

		if (i < num_out - 1) {
			prev = virtq_alloc_desc(vq);
			vq->desc[prev - 1].next = prev;
		}
	}

	// Add input buffers (device writes)
	for (uint16_t i = 0; i < num_in; i++) {
		uint16_t idx;
		if (i == 0 && num_out == 0) {
			idx = head;
		} else {
			idx = virtq_alloc_desc(vq);
			vq->desc[prev].next = idx;
			prev = idx;
		}

		uint64_t phys_addr = (uint64_t) (buffers[num_out + i]);
		// uint64_t phys_addr = (uint64_t) buffers[i];
		vq->desc[idx].addr = phys_addr;
		vq->desc[idx].len = lengths[num_out + i];
		vq->desc[idx].flags = VIRTQ_DESC_F_WRITE;

		log(mod, "IN desc[%d]: phys=0x%x, len=%d, flags=0x%x", idx,
		    phys_addr, lengths[num_out + i], vq->desc[idx].flags);

		if (i < num_in - 1) {
			vq->desc[idx].flags |= VIRTQ_DESC_F_NEXT;
			prev = idx;
		}
	}

	// Remove NEXT flag from last descriptor
	if (num_in > 0) {
		vq->desc[prev].flags &= ~VIRTQ_DESC_F_NEXT;
		log(mod, "Last IN desc[%d]: flags=0x%x (NEXT removed)", prev,
		    vq->desc[prev].flags);
	} else if (num_out > 0) {
		vq->desc[prev].flags &= ~VIRTQ_DESC_F_NEXT;
		log(mod, "Last OUT desc[%d]: flags=0x%x (NEXT removed)", prev,
		    vq->desc[prev].flags);
	}

	return 0;
}

void VirtioGpu::virtq_kick(uint16_t queue_index) {
	// Strong memory barrier before notifying device
	asm volatile("mfence" ::: "memory");

	uint32_t notify_addr =
		notify_offset_ + queue_index * notify_multiplier_;
	volatile uint32_t* notify_reg =
		(volatile uint32_t*) ((uintptr_t) notify_base_ + notify_addr);

	log(mod,
	    "Notifying queue %d at address 0x%x (notify_base=%0x%x, "
	    "offset=0x%x, "
	    "multiplier=0x%x)",
	    queue_index, notify_reg, notify_base_, notify_offset_,
	    notify_multiplier_);

	// Write the queue index to notify the device
	*notify_reg = queue_index;

	// Strong memory barrier after notification
	asm volatile("mfence" ::: "memory");
}

int VirtioGpu::virtq_get_used_elem(struct virtio_gpu_queue* vq, uint16_t* id,
				   uint32_t* len) {
	// Memory barrier to ensure we see updated used ring
	asm volatile("" ::: "memory");

	if (vq->last_used_idx == vq->used->idx) {
		return -1;
	}

	uint16_t used_idx = vq->last_used_idx % vq->queue_size;
	struct virtq_used_elem* elem = &vq->used->ring[used_idx];

	*id = elem->id;
	*len = elem->len;
	vq->last_used_idx++;

	return 0;
}

// ---------------- VirtIO GPU Commands ----------------
int VirtioGpu::virtio_gpu_send_command(void* cmd, uint32_t cmd_size, void* resp,
				       uint32_t resp_size) {
	void* buffers[2];
	uint32_t lengths[2];
	uint16_t num_out = 0, num_in = 0;

	// Setup command buffer (output - device reads)
	if (cmd && cmd_size > 0) {
		buffers[0] = cmd;
		lengths[0] = cmd_size;
		num_out = 1;
		log(mod, "Command buffer: virt=%x, size=%d", cmd, cmd_size);
	}

	// Setup response buffer (input - device writes)
	if (resp && resp_size > 0) {
		buffers[num_out] = resp;
		lengths[num_out] = resp_size;
		num_in = 1;
		log(mod, "Response buffer: virt=%x, size=%d", resp, resp_size);
	}

	uint16_t head;
	if (virtq_add_buf(&control_queue_, buffers, lengths, num_out, num_in,
			  &head)
	    < 0) {
		log(mod, "Failed to add buffer to virtqueue");
		return -1;
	}

	// Add to available ring
	// avail->idx dipakai langsung (uint16 wrap natural), ring yang di-mod
	uint16_t avail_idx =
		control_queue_.avail->idx % control_queue_.queue_size;
	control_queue_.avail->ring[avail_idx] = head;

	// Memory barrier sebelum update avail->idx agar device melihat
	// descriptor sebelum melihat idx yang baru
	asm volatile("mfence" ::: "memory");

	control_queue_.avail->idx++;

	log(mod, "Added buffer head=%d to avail ring[%d], new avail->idx=%d",
	    head, avail_idx, control_queue_.avail->idx);

	// Notify device
	virtq_kick(0);

	int timeout = 5000000;

	for (int i = 0; i < timeout; i++) {
		// Pastikan CPU melihat perubahan memori dari device
		asm volatile("" ::: "memory");

		uint16_t used_id;
		uint32_t used_len;

		// Konsumsi semua used element yang tersedia sampai kita temukan
		// head kita. Elemen lain yang bukan milik kita di-log tapi tidak
		// dibuang begitu saja — idealnya masuk pending queue, tapi di sini
		// kita skip dan lanjut polling agar tidak hang.
		while (virtq_get_used_elem(&control_queue_, &used_id, &used_len)
		       == 0) {
			log(mod,
			    "Got used element: id=%d, len=%d (expected "
			    "head=%d)",
			    used_id, used_len, head);

			if (used_id == head) {
				// Bebaskan descriptor chain
				uint16_t desc_idx = used_id;
				while (desc_idx < control_queue_.queue_size
				       && (control_queue_.desc[desc_idx].flags
					   & VIRTQ_DESC_F_NEXT)) {
					uint16_t next =
						control_queue_.desc[desc_idx]
							.next;
					virtq_free_desc(&control_queue_,
							desc_idx);
					desc_idx = next;
				}
				if (desc_idx < control_queue_.queue_size) {
					virtq_free_desc(&control_queue_,
							desc_idx);
				}

				log(mod,
				    "Command completed successfully (head=%d)",
				    head);
				return 0;
			} else {
				// Used element bukan milik command ini.
				// Descriptor tetap sudah di-consume dari ring;
				// catat saja dan lanjutkan polling.
				log(mod,
				    "Unexpected used id=%d (expected %d), "
				    "skipping",
				    used_id, head);
			}
		}

		// Interrupt hanya sebagai sinyal: reset flag lalu re-check used ring
		// di iterasi berikutnya. Jangan break di sini!
		if (interrupt_exist) {
			log(mod, "ISR triggered, re-checking used ring");
			interrupt_exist = false;
			// Tidak break — kembali ke atas untuk cek used ring
			continue;
		}

		IOUtils::sleep(5);

		if (i % 100000 == 0) {
			log(mod, "Still waiting for response... (i=%d)", i);
		}
	}

	// Timeout — dump state untuk debug
	log(mod, "Timeout waiting for command response (head=%d)", head);
	log(mod, "Used ring state: last_used_idx=%d, used->idx=%d",
	    control_queue_.last_used_idx, control_queue_.used->idx);

	log(mod, "Descriptor chain for head=%d:", head);
	uint16_t desc_idx = head;
	int chain_len = 0;
	while (desc_idx < control_queue_.queue_size && chain_len < 10) {
		log(mod, "  desc[%d]: addr=0x%lx, len=%d, flags=0x%x, next=%d",
		    desc_idx, control_queue_.desc[desc_idx].addr,
		    control_queue_.desc[desc_idx].len,
		    control_queue_.desc[desc_idx].flags,
		    control_queue_.desc[desc_idx].next);

		if (!(control_queue_.desc[desc_idx].flags & VIRTQ_DESC_F_NEXT))
			break;

		desc_idx = control_queue_.desc[desc_idx].next;
		chain_len++;
	}

	return -1;
}
