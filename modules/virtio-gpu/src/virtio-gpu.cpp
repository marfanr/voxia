#include "cpu/core.h"
#include "graphic.h"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge.hpp"
#include "type.h"
#include <cpu/irq_lock.h>
#include <procc/sched.h>
#include <procc/workqueue.h>
#include <str.h>
#include <virtio-gpu/virtio-gpu.hpp>

#define ALIGN_UP(x, align) (((x) + (align) - 1) & ~((align) - 1))

#define ENABLE_DEBUG false

// VIRGL_CMD0 macro \u2014 defines ada di header virtio-gpu.hpp

struct virtio_gpu_queue VirtioGpu::control_queue_ = {};
struct virtio_gpu_queue VirtioGpu::cursor_queue_ = {};
spinlock_t VirtioGpu::controlq_lock_ = SPINLOCK_INIT;
spinlock_t VirtioGpu::cursorq_lock_ = SPINLOCK_INIT;

static volatile boolean_t interrupt_exist = 0;

VirtioGpu* g_virtio_gpu = nullptr;

void VirtioGpu::setup() {
	g_virtio_gpu = this;

	uint8_t common_bar = dev_->common_cfg.bar & 0x7;
	log(mod, "common config BAR: %d", common_bar);

	uintptr_t common_addr =
	    dev_->pci.bar[common_bar].address + dev_->common_cfg.offset;
	log(mod, "common config addr: 0x%lx (offset 0x%lx)", common_addr,
	    dev_->common_cfg.offset);

	volatile virtio_pci_common_cfg_t* common_cfg =
	    (volatile virtio_pci_common_cfg_t*)common_addr;

	uint8_t notify_bar = dev_->notify_cfg.bar & 0x7;
	uintptr_t notify_base =
	    dev_->pci.bar[notify_bar].address + dev_->notify_cfg.offset;
	notify_base_ = notify_base;
	log(mod, "notify config addr: 0x%lx", notify_base);

	notify_multiplier_ = dev_->notify_cfg.length;

	if (dev_->device_cfg.length >= 12) {
		uint8_t dev_cfg_bar = dev_->device_cfg.bar & 0x7;
		if (dev_cfg_bar < 6) {
			uintptr_t dev_cfg_addr =
			    dev_->pci.bar[dev_cfg_bar].address +
			    dev_->device_cfg.offset;
			if (dev_->pci.bar[dev_cfg_bar].address != 0) {
				max_scanouts =
				    *(volatile uint32_t*)(dev_cfg_addr + 8);
			}
		}
	}
	log(mod, "Device config: max supported scanouts = %d", max_scanouts);

	// Reset device
	common_cfg->device_status = 0;
	// Acknowledge device
	common_cfg->device_status |= VIRTIO_STATUS_ACKNOWLEDGE;
	// Set driver bit
	common_cfg->device_status |= VIRTIO_STATUS_DRIVER;

	// Read device features
	common_cfg->device_feature_select = 0;
	uint32_t device_features_low = common_cfg->device_feature;
	common_cfg->device_feature_select = 1;
	uint32_t device_features_high = common_cfg->device_feature;
	log(mod, "Device features: 0x%x 0x%x", device_features_high,
	    device_features_low);

	// Write driver features
	common_cfg->driver_feature_select = 0;

	bool has_virgl = (device_features_low & VIRTIO_GPU_F_VIRGL) != 0;
	bool has_edid = (device_features_low & VIRTIO_GPU_F_EDID) != 0;
	bool has_ctx_init = (device_features_low & (1 << 2)) != 0;
	log(mod, "Host supports VirGL: %s, EDID: %s, CTX_INIT: %s",
	    has_virgl ? "YES" : "NO", has_edid ? "YES" : "NO",
	    has_ctx_init ? "YES" : "NO");

	if (!has_ctx_init) {
		log(mod, "WARNING: Host QEMU does NOT offer "
		         "VIRTIO_GPU_F_CONTEXT_INIT! Venus requires this.");
	}

	uint32_t requested_features_low = device_features_low;
	common_cfg->driver_feature = requested_features_low;

	common_cfg->driver_feature_select = 1;

	// MUST set bit 32 (VIRTIO_F_VERSION_1) for Virtio 1.0
	uint32_t driver_features_high = device_features_high & 0x1;
	common_cfg->driver_feature = driver_features_high;
	log(mod, "Driver features negotiated: high=0x%x low=0x%x",
	    driver_features_high, requested_features_low);

	// Set FEATURES_OK
	common_cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;

	// Verify features
	// Need a barrier to ensure device has updated status
	asm volatile("mfence" ::: "memory");
	if (!(common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
		log(mod,
		    "Feature negotiation failed (device rejected features, "
		    "status=0x%x)",
		    (uint32_t)common_cfg->device_status);
		return;
	}
	log(mod, "Features OK");

	// Device-specific setup
	common_cfg->queue_select = 0;
	uint16_t queue_size = common_cfg->queue_size;
	if (queue_size == 0 || queue_size > VIRTIO_GPU_QUEUE_SIZE) {
		queue_size = VIRTIO_GPU_QUEUE_SIZE;
	}
	common_cfg->queue_size = queue_size;
	log(mod, "Queue size: %d", queue_size);

	// Allocate and initialize virtqueue
	uintptr_t desc_size = queue_size * sizeof(struct virtq_desc);
	uintptr_t avail_off = desc_size;
	uintptr_t avail_size = sizeof(uint16_t) * (3 + queue_size);
	uintptr_t used_off =
	    ALIGN_UP(avail_off + avail_size, VIRTIO_GPU_QUEUE_ALIGN);
	uintptr_t used_size = sizeof(struct virtq_used) +
	                      sizeof(struct virtq_used_elem) * queue_size;
	size_t vq_total_size =
	    ALIGN_UP(used_off + used_size, VIRTIO_GPU_QUEUE_ALIGN);

	uintptr_t vq_phys = 0;
	void* vq_mem = IOForge::IOUtils::DMAAlloc(vq_total_size, &vq_phys);

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
	    (uintptr_t)control_queue_.used - (uintptr_t)vq_mem;
	common_cfg->queue_used = vq_phys + used_offset;

	log(mod, "Virtqueue configured: desc=0x%lx, avail=0x%lx, used=0x%lx",
	    (uint64_t)common_cfg->queue_desc, (uint64_t)common_cfg->queue_avail,
	    (uint64_t)common_cfg->queue_used);

	// Enable queue
	common_cfg->queue_enable = 1;
	log(mod, "Control Queue enabled");

	// Initialize cursor queue (queue 1)
	common_cfg->queue_select = 1;
	uint16_t cursor_queue_size = common_cfg->queue_size;
	if (cursor_queue_size == 0 ||
	    cursor_queue_size > VIRTIO_GPU_QUEUE_SIZE) {
		cursor_queue_size = VIRTIO_GPU_QUEUE_SIZE;
	}
	common_cfg->queue_size = cursor_queue_size;
	uintptr_t cvq_phys = 0;
	void* cvq_mem = IOForge::IOUtils::DMAAlloc(vq_total_size, &cvq_phys);
	if (cvq_mem) {
		memset(cvq_mem, 0, vq_total_size);
		virtq_init(&cursor_queue_, cvq_mem, cursor_queue_size,
		           cvq_phys);
		common_cfg->queue_desc = cvq_phys;
		common_cfg->queue_avail =
		    cvq_phys + cursor_queue_size * sizeof(struct virtq_desc);
		uintptr_t cused_offset =
		    (uintptr_t)cursor_queue_.used - (uintptr_t)cvq_mem;
		common_cfg->queue_used = cvq_phys + cused_offset;
		common_cfg->queue_enable = 1;
		log(mod, "Cursor Queue enabled");
	}

	common_cfg->queue_select = 0;
	uint16_t q_noff = common_cfg->queue_notify_off;
	notify_offset_[0] = q_noff * notify_multiplier_;
	log(mod, "queue 0 notify_offset: 0x%lx", notify_offset_[0]);

	common_cfg->queue_select = 1;
	q_noff = common_cfg->queue_notify_off;
	notify_offset_[1] = q_noff * notify_multiplier_;
	log(mod, "queue 1 notify_offset: 0x%lx", notify_offset_[1]);

	for (int i = 0; i < 4; i++) {
		common_cfg->queue_select = i;
		asm volatile("mfence" ::: "memory");
		log(mod, "queue %d: queue_size=%d, notify_off=0x%x", i,
		    common_cfg->queue_size, common_cfg->queue_notify_off);
	}
	common_cfg->queue_select = 0;
	asm volatile("mfence" ::: "memory");

	// Set DRIVER_OK
	common_cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
	log(mod, "Driver OK");

	// FIX: Add delay to ensure device is ready
	IOForge::IOUtils::sleep(1000);

	log(mod, "VirtIO GPU initialized successfully");

	// irq
	if (dev_->pci.interrupt_line) {
		isr_irq_register(dev_->pci.interrupt_line,
		                 (void*)VirtioGpu::fireHandler);
	}

	// Gathering Scanout Information
	virtio_gpu_get_display_info();

	// test capset
	{
		uintptr_t test_phys = 0;
		struct virtio_gpu_get_capset_info* test_cmd =
		    (struct virtio_gpu_get_capset_info*)ioforge_dma_alloc(
		        sizeof(*test_cmd), &test_phys);

		test_cmd->hdr.type = VIRTIO_GPU_CMD_GET_CAPSET_INFO;
		test_cmd->hdr.flags = 0;
		test_cmd->hdr.padding = 0;
		test_cmd->capset_index = 0;

		uintptr_t test_resp_phys = 0;
		struct virtio_gpu_capset_info_response* test_resp =
		    (struct virtio_gpu_capset_info_response*)ioforge_dma_alloc(
		        sizeof(struct virtio_gpu_capset_info_response),
		        &test_resp_phys);

		if (!test_cmd || !test_resp) {
			log("VirtioGpu",
			    "Failed to allocate test buffers for capset");
			return;
		}

		int result = g_virtio_gpu->virtio_gpu_send_command(
		    (void*)test_phys, sizeof(*test_cmd), (void*)test_resp_phys,
		    sizeof(*test_resp));
		if (result == 0) {
			log("VIRTIOGPU", "Capset communication test PASSED");
			log("VIRTIOGPU", "Capset ID: %d, Version: %d, Size: %d",
			    test_resp->capset_id, test_resp->capset_max_version,
			    test_resp->capset_max_size);
		} else {
			log("VIRTIOGPU", "Capset communication test FAILED");
		}

		IOForge::IOUtils::DMAFree((void*)test_phys, (void*)test_cmd,
		                          sizeof(*test_cmd));
		IOForge::IOUtils::DMAFree((void*)test_resp_phys,
		                          (void*)test_resp, sizeof(*test_resp));
	}
}

extern "C" void wq_get_display_info(void* arg) {
	(void)arg;
	log("VIRTIOGPU", "wq_get_display_info started");
	if (g_virtio_gpu) {
		uintptr_t test_phys = 0;
		struct virtio_gpu_ctrl_hdr* test_cmd =
		    (struct virtio_gpu_ctrl_hdr*)IOForge::IOUtils::DMAAlloc(
		        sizeof(*test_cmd), &test_phys);

		uintptr_t test_resp_phys = 0;
		struct virtio_gpu_resp_display_info* test_resp =
		    (struct virtio_gpu_resp_display_info*)
		        IOForge::IOUtils::DMAAlloc(
		            sizeof(struct virtio_gpu_resp_display_info),
		            &test_resp_phys);

		if (!test_cmd || !test_resp) {
			log("VirtioGpu", "Failed to allocate test buffers");
			if (test_cmd)
				IOForge::IOUtils::DMAFree((void*)test_phys,
				                          test_cmd,
				                          sizeof(*test_cmd));
			if (test_resp)
				IOForge::IOUtils::DMAFree((void*)test_resp_phys,
				                          test_resp,
				                          sizeof(*test_resp));
			return;
		}

		// Test communication
		memset(test_cmd, 0, sizeof(*test_cmd));
		test_cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

		memset(test_resp, 0, sizeof(*test_resp));

		log("VIRTIOGPI", "Test buffers allocated: cmd=0x%x, resp=0x%x",
		    test_cmd, test_resp);

		int result = g_virtio_gpu->virtio_gpu_send_command(
		    (void*)test_phys, sizeof(*test_cmd), (void*)test_resp_phys,
		    sizeof(*test_resp));

		if (result == 0) {
			log("VIRTIOGPU", "GPU communication test PASSED");
			if (test_resp->hdr.type ==
			    VIRTIO_GPU_RESP_OK_DISPLAY_INFO) {
				int enabled_count = 0;
				for (int i = 0; i < 16; i++) {
					if (test_resp->pmodes[i].enabled) {
						log("VIRTIOGPU",
						    "Scanout %d: %dx%d at "
						    "(%d,%d)",
						    i,
						    test_resp->pmodes[i]
						        .rect.width,
						    test_resp->pmodes[i]
						        .rect.height,
						    test_resp->pmodes[i].rect.x,
						    test_resp->pmodes[i]
						        .rect.y);
						enabled_count++;
					}
				}
				if (enabled_count == 0) {
					log("VIRTIOGPU",
					    "No enabled display modes found "
					    "yet. "
					    "Waiting for config interrupt...");
				}
			} else {
				log("VIRTIOGPU",
				    "Unexpected response type: 0x%x",
				    test_resp->hdr.type);
			}
		} else {
			log("VIRTIOGPU", "GPU communication test FAILED");
		}
		IOForge::IOUtils::DMAFree((void*)test_phys, (void*)test_cmd,
		                          sizeof(*test_cmd));
		IOForge::IOUtils::DMAFree((void*)test_resp_phys,
		                          (void*)test_resp, sizeof(*test_resp));
	}
}

void VirtioGpu::fireHandler() {
	auto instance = VirtioGpu::getInstance();
	uint8_t isr_bar = instance->dev_->isr_cfg.bar & 0x7;
	uintptr_t isr_addr = instance->dev_->pci.bar[isr_bar].address +
	                     instance->dev_->isr_cfg.offset;

	log(instance->mod, "virtio fired...\n");
	uint8_t isr = *(uint8_t*)isr_addr;
	if (isr & VIRTIO_ISR_QUEUE_INT) {
		log(instance->mod,
		    "Queue interrupt received! Waking threads...");
		interrupt_exist = true;

		uint16_t last_idx = instance->control_queue_.last_used_idx;
		uint16_t curr_idx = instance->control_queue_.used->idx;

		while (last_idx != curr_idx) {
			uint16_t ring_idx =
			    last_idx % instance->control_queue_.queue_size;
			uint32_t head_id =
			    instance->control_queue_.used->ring[ring_idx].id;

			// Bangunkan thread yang menunggunya!
			thread_t* waiter = instance->waiting_threads[head_id];
			if (waiter) {
				vxThreadWake(waiter);
				instance->waiting_threads[head_id] = nullptr;
			}
			last_idx++;
		}
	}

	if (isr & VIRTIO_ISR_DEV_CFG_INT) {
		log(instance->mod, "Config change interrupt received.");
		vxAddWorkqueueTask(wq_get_display_info, nullptr, nullptr);
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
	uintptr_t base = (uintptr_t)vq_mem;

	vq->desc = (struct virtq_desc*)base;

	uintptr_t avail_off = queue_size * sizeof(struct virtq_desc);
	vq->avail = (struct virtq_avail*)(base + avail_off);

	// avail size = flags(2) + idx(2) + ring[queue_size](2 each) +
	// used_event(2)
	uintptr_t used_off = avail_off + sizeof(uint16_t) * (3 + queue_size);
	used_off = ALIGN_UP(used_off, VIRTIO_GPU_QUEUE_ALIGN);
	vq->used = (struct virtq_used*)(base + used_off);

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
	       sizeof(struct virtq_used) +
	           sizeof(struct virtq_used_elem) * queue_size);

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

	if (ENABLE_DEBUG)
		log(mod,
		    "Building descriptor chain, head=%d, num_out=%d, num_in=%d",
		    head, num_out, num_in);

	// Add output buffers (device reads)
	for (uint16_t i = 0; i < num_out; i++) {
		// uint64_t phys_addr = virt_to_phys(buffers[i]);
		uint64_t phys_addr = (uint64_t)buffers[i];
		vq->desc[prev].addr = phys_addr;
		vq->desc[prev].len = lengths[i];
		vq->desc[prev].flags = VIRTQ_DESC_F_NEXT;

		if (ENABLE_DEBUG)
			log(mod,
			    "OUT desc[%d]: phys=0x%x, len=%d, flags=0x%x, "
			    "next=%d",
			    prev, phys_addr, lengths[i], vq->desc[prev].flags,
			    vq->desc[prev].next);

		if (i < num_out - 1) {
			uint16_t next_idx = virtq_alloc_desc(vq);
			vq->desc[prev].next = next_idx;
			prev = next_idx;
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

		uint64_t phys_addr = (uint64_t)(buffers[num_out + i]);
		// uint64_t phys_addr = (uint64_t) buffers[i];
		vq->desc[idx].addr = phys_addr;
		vq->desc[idx].len = lengths[num_out + i];
		vq->desc[idx].flags = VIRTQ_DESC_F_WRITE;

		if (ENABLE_DEBUG)
			log(mod, "IN desc[%d]: phys=0x%x, len=%d, flags=0x%x",
			    idx, phys_addr, lengths[num_out + i],
			    vq->desc[idx].flags);

		if (i < num_in - 1) {
			vq->desc[idx].flags |= VIRTQ_DESC_F_NEXT;
			prev = idx;
		}
	}

	// Remove NEXT flag from last descriptor
	if (num_in > 0) {
		vq->desc[prev].flags &= ~VIRTQ_DESC_F_NEXT;
		if (ENABLE_DEBUG)
			log(mod, "Last IN desc[%d]: flags=0x%x (NEXT removed)",
			    prev, vq->desc[prev].flags);
	} else if (num_out > 0) {
		vq->desc[prev].flags &= ~VIRTQ_DESC_F_NEXT;
		if (ENABLE_DEBUG)
			log(mod, "Last OUT desc[%d]: flags=0x%x (NEXT removed)",
			    prev, vq->desc[prev].flags);
	}

	return 0;
}

void VirtioGpu::virtq_kick(uint16_t queue_index) {
	// Strong memory barrier before notifying device
	asm volatile("mfence" ::: "memory");

	if (queue_index > 1)
		queue_index = 0; // safety

	uint32_t notify_addr = notify_offset_[queue_index];
	volatile uint16_t* notify_reg =
	    (volatile uint16_t*)((uintptr_t)notify_base_ + notify_addr);

	if (ENABLE_DEBUG)
		log(mod,
		    "Notifying queue %d at address 0x%lx (notify_base=0x%lx, "
		    "notify_offset=0x%lx, "
		    "multiplier=0x%x)",
		    queue_index, (uintptr_t)notify_reg, (uintptr_t)notify_base_,
		    notify_offset_[queue_index], notify_multiplier_);

	// Write the queue index to notify the device (16-bit per spec)
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

int VirtioGpu::virtio_gpu_send_command(void* cmd, uint32_t cmd_size, void* resp,
                                       uint32_t resp_size, int queue_idx) {
	struct virtio_gpu_queue* vq =
	    (queue_idx == 1) ? &cursor_queue_ : &control_queue_;
	spinlock_t* lock = (queue_idx == 1) ? &cursorq_lock_ : &controlq_lock_;

	uintptr_t flags = irq_save();
	spin_acquire(lock);

	void* buffers[2];
	uint32_t lengths[2];
	uint16_t num_out = 0, num_in = 0;

	if (cmd && cmd_size > 0) {
		buffers[0] = cmd;
		lengths[0] = cmd_size;
		num_out = 1;
		if (ENABLE_DEBUG)
			log(mod, "Command buffer: phys=0x%lx, size=%d",
			    (uintptr_t)cmd, cmd_size);
	}

	if (resp && resp_size > 0) {
		buffers[num_out] = resp;
		lengths[num_out] = resp_size;
		num_in = 1;
		if (ENABLE_DEBUG)
			log(mod, "Response buffer: phys=0x%lx, size=%d",
			    (uintptr_t)resp, resp_size);
	}

	uint16_t head;
	if (virtq_add_buf(vq, buffers, lengths, num_out, num_in, &head) < 0) {
		log(mod, "Failed to add buffer to virtqueue");
		spin_release(lock);
		irq_restore(flags);
		return -1;
	}

	// Add to available ring
	uint16_t avail_idx = vq->avail->idx % vq->queue_size;
	vq->avail->ring[avail_idx] = head;

	asm volatile("mfence" ::: "memory");

	vq->avail->idx++;

	if (ENABLE_DEBUG)
		log(mod,
		    "Added buffer head=%d to avail ring[%d], new avail->idx=%d",
		    head, avail_idx, vq->avail->idx);

	if (queue_idx == 0 && vxIsSchedulerRunning()) {
		waiting_threads[head] = get_current_thread();
	}

	virtq_kick(queue_idx);

	// Increase timeout significantly because 3D operations (like shader
	// compilation) can take a long time on the host. If this times out
	// prematurely, descriptors leak!
	uint64_t timeout = 5000000000ULL;

	for (uint64_t i = 0; i < timeout; i++) {
		asm volatile("" ::: "memory");

		uint16_t used_id;
		uint32_t used_len;

		while (virtq_get_used_elem(vq, &used_id, &used_len) == 0) {
			if (ENABLE_DEBUG)
				log(mod,
				    "Got used element: id=%d, len=%d (expected "
				    "head=%d)",
				    used_id, used_len, head);

			if (used_id == head) {
				// Bebaskan descriptor chain
				uint16_t desc_idx = used_id;
				while (desc_idx < vq->queue_size &&
				       (vq->desc[desc_idx].flags &
				        VIRTQ_DESC_F_NEXT)) {
					uint16_t next = vq->desc[desc_idx].next;
					virtq_free_desc(vq, desc_idx);
					desc_idx = next;
				}
				if (desc_idx < vq->queue_size) {
					virtq_free_desc(vq, desc_idx);
				}

				if (ENABLE_DEBUG)
					log(mod,
					    "Command completed successfully "
					    "(head=%d)",
					    head);
				spin_release(lock);
				irq_restore(flags);
				return 0;
			} else {
				log(mod,
				    "Unexpected used id=%d (expected %d), "
				    "skipping and freeing old descriptors!",
				    used_id, head);

				// Bebaskan descriptor chain untuk command yang
				// sudah timeout sebelumnya
				uint16_t desc_idx = used_id;
				while (desc_idx < vq->queue_size &&
				       (vq->desc[desc_idx].flags &
				        VIRTQ_DESC_F_NEXT)) {
					uint16_t next = vq->desc[desc_idx].next;
					virtq_free_desc(vq, desc_idx);
					desc_idx = next;
				}
				if (desc_idx < vq->queue_size) {
					virtq_free_desc(vq, desc_idx);
				}
			}
		}

		if (interrupt_exist) {
			log(mod, "ISR triggered, re-checking used ring");
			interrupt_exist = false;
			continue;
		}

		if (irq_is_enabled()) {
			IOForge::IOUtils::sleep(5);
		}

		if (irq_is_enabled() && vxIsSchedulerRunning()) {
			schedule_yield();
		}
	}

	// Timeout — dump state untuk debug
	log(mod, "Timeout waiting for command response (head=%d)", head);
	log(mod, "Used ring state: last_used_idx=%d, used->idx=%d",
	    vq->last_used_idx, vq->used->idx);

	log(mod, "Descriptor chain for head=%d:", head);
	uint16_t desc_idx = head;
	int chain_len = 0;
	while (desc_idx < vq->queue_size && chain_len < 10) {
		log(mod, "  desc[%d]: addr=0x%lx, len=%d, flags=0x%x, next=%d",
		    desc_idx, vq->desc[desc_idx].addr, vq->desc[desc_idx].len,
		    vq->desc[desc_idx].flags, vq->desc[desc_idx].next);

		if (!(vq->desc[desc_idx].flags & VIRTQ_DESC_F_NEXT))
			break;

		desc_idx = vq->desc[desc_idx].next;
		chain_len++;
	}

	spin_release(lock);
	irq_restore(flags);
	return -1;
}

int VirtioGpu::virtio_gpu_update_cursor(uint32_t resource_id,
                                        uint32_t scanout_id, uint32_t x,
                                        uint32_t y, uint32_t hot_x,
                                        uint32_t hot_y) {
	uintptr_t cmd_phys = 0;
	struct virtio_gpu_update_cursor* cmd =
	    (struct virtio_gpu_update_cursor*)IOForge::IOUtils::DMAAlloc(
	        sizeof(*cmd), &cmd_phys);

	cmd->hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
	cmd->hdr.flags = 0;
	cmd->hdr.fence_id = 0;
	cmd->hdr.ctx_id = 0;
	cmd->hdr.padding = 0;

	cmd->pos.scanout_id = scanout_id;
	cmd->pos.x = x;
	cmd->pos.y = y;
	cursor_x_ = x;
	cursor_y_ = y;
	cmd->pos.padding = 0;

	cmd->resource_id = resource_id;
	cursor_res_id_ = resource_id; // Simpan ID baru agar saat MOVE_CURSOR
	                              // tidak memakai ID lama
	cmd->hot_x = hot_x;
	cmd->hot_y = hot_y;
	cursor_hot_x_ = hot_x;
	cursor_hot_y_ = hot_y;
	cmd->padding = 0;

	uintptr_t resp_phys = 0;
	struct virtio_gpu_ctrl_hdr* resp =
	    (struct virtio_gpu_ctrl_hdr*)IOForge::IOUtils::DMAAlloc(
	        sizeof(*resp), &resp_phys);
	if (!resp || !cmd) {
		log(mod, "Failed to allocate UPDATE_CURSOR buffers");
		return -3;
	}

	memset(resp, 0, sizeof(*resp));

	if (!cmd || !resp) {
		log(mod, "Failed to allocate UPDATE_CURSOR buffers");
		if (cmd)
			IOForge::IOUtils::DMAFree((void*)cmd_phys, (void*)cmd,
			                          sizeof(*cmd));
		if (resp)
			IOForge::IOUtils::DMAFree((void*)resp_phys, (void*)resp,
			                          sizeof(*resp));
		return -1;
	}

	if (virtio_gpu_send_command((void*)cmd_phys, sizeof(*cmd),
	                            (void*)resp_phys, sizeof(*resp), 1) < 0) {
		log(mod, "Failed to send UPDATE_CURSOR command");
		IOForge::IOUtils::DMAFree((void*)cmd_phys, (void*)cmd,
		                          sizeof(*cmd));
		IOForge::IOUtils::DMAFree((void*)resp_phys, (void*)resp,
		                          sizeof(*resp));
		return -1;
	}

	IOForge::IOUtils::DMAFree((void*)cmd_phys, (void*)cmd, sizeof(*cmd));
	IOForge::IOUtils::DMAFree((void*)resp_phys, (void*)resp, sizeof(*resp));
	return 0;
}

static uintptr_t move_cursor_cmd_phys = 0;
static struct virtio_gpu_update_cursor* move_cursor_cmd = 0;
static uintptr_t move_cursor_resp_phys = 0;
struct virtio_gpu_ctrl_hdr* move_cursor_resp = 0;

int VirtioGpu::virtio_gpu_move_cursor(uint32_t scanout_id, uint32_t x,
                                      uint32_t y) {
	if (!cursor_res_id_)
		return -1;

	if (!move_cursor_cmd_phys) {
		move_cursor_cmd = (struct virtio_gpu_update_cursor*)
		    IOForge::IOUtils::DMAAlloc(sizeof(*move_cursor_cmd),
		                               &move_cursor_cmd_phys);
		if (!move_cursor_cmd || !move_cursor_cmd_phys) {
			return -3;
		}
	}
	memset(move_cursor_cmd, 0, sizeof(*move_cursor_cmd));

	move_cursor_cmd->hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
	move_cursor_cmd->hdr.flags = 0;
	move_cursor_cmd->hdr.fence_id = 0;
	move_cursor_cmd->hdr.ctx_id = 0;
	move_cursor_cmd->hdr.padding = 0;

	move_cursor_cmd->pos.scanout_id = scanout_id;
	move_cursor_cmd->pos.x = x;
	move_cursor_cmd->pos.y = y;
	cursor_x_ = x;
	cursor_y_ = y;
	move_cursor_cmd->pos.padding = 0;

	move_cursor_cmd->resource_id = cursor_res_id_;
	move_cursor_cmd->hot_x = cursor_hot_x_;
	move_cursor_cmd->hot_y = cursor_hot_y_;
	move_cursor_cmd->padding = 0;

	if (!move_cursor_resp_phys) {
		move_cursor_resp =
		    (struct virtio_gpu_ctrl_hdr*)IOForge::IOUtils::DMAAlloc(
		        sizeof(*move_cursor_resp), &move_cursor_resp_phys);
	}
	memset(move_cursor_resp, 0, sizeof(*move_cursor_resp));

	if (!move_cursor_cmd || !move_cursor_resp) {
		log(mod, "Failed to allocate MOVE_CURSOR buffers");
		return -1;
	}

	if (virtio_gpu_send_command((void*)move_cursor_cmd_phys,
	                            sizeof(*move_cursor_cmd),
	                            (void*)move_cursor_resp_phys,
	                            sizeof(*move_cursor_resp), 1) < 0) {
		log(mod, "Failed to send MOVE_CURSOR command");
		return -1;
	}
	return 0;
}

#undef ENABLE_DEBUG