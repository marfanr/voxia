#include "e1000/e1000.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.h"
#include "type.h"

#define IO_ADDR_OFFSET 0x0
#define IO_DATA_OFFSET 0x4

#define REG_CTRL 0x0000
#define REG_STATUS 0x0008
#define REG_EEPROM 0x0014
#define REG_CTRL_EXT 0x0018
#define REG_IMASK 0x00D0
#define REG_RCTRL 0x0100
#define REG_RXDESCLO 0x2800
#define REG_RXDESCHI 0x2804
#define REG_RXDESCLEN 0x2808
#define REG_RXDESCHEAD 0x2810
#define REG_RXDESCTAIL 0x2818

#define IMS_RXT0 (1 << 7)
#define IMS_RXO (1 << 6)
#define IMS_RXDMT0 (1 << 4)
#define IMS_LSC (1 << 2)

#define IMS_RXQ0 (1 << 20)  // Bit 20: Receive Queue 0
#define IMS_TXQ0 (1 << 22)  // Bit 22: Transmit Queue 0
#define IMS_OTHER (1 << 24) // Bit 24: Other Causes (LSC, dll)

#define REG_TCTRL 0x0400
#define REG_TXDESCLO 0x3800
#define REG_TXDESCHI 0x3804
#define REG_TXDESCLEN 0x3808
#define REG_TXDESCHEAD 0x3810
#define REG_TXDESCTAIL 0x3818

#define REG_RDTR 0x2820	  // RX Delay Timer Register
#define REG_RXDCTL 0x2828 // RX Descriptor Control
#define REG_RADV 0x282C	  // RX Int. Absolute Delay Timer
#define REG_RSRPD 0x2C00  // RX Small Packet Detect Interrupt

#define REG_TIPG 0x0410 // Transmit Inter Packet Gap
#define ECTRL_SLU 0x40	// set link up

#define RCTL_EN (1 << 1)	    // Receiver Enable
#define RCTL_SBP (1 << 2)	    // Store Bad Packets
#define RCTL_UPE (1 << 3)	    // Unicast Promiscuous Enabled
#define RCTL_MPE (1 << 4)	    // Multicast Promiscuous Enabled
#define RCTL_LPE (1 << 5)	    // Long Packet Reception Enable
#define RCTL_LBM_NONE (0 << 6)	    // No Loopback
#define RCTL_LBM_PHY (3 << 6)	    // PHY or external SerDesc loopback
#define RTCL_RDMTS_HALF (0 << 8)    // Free Buffer Threshold is 1/2 of RDLEN
#define RTCL_RDMTS_QUARTER (1 << 8) // Free Buffer Threshold is 1/4 of RDLEN
#define RTCL_RDMTS_EIGHTH (2 << 8)  // Free Buffer Threshold is 1/8 of RDLEN
#define RCTL_MO_36 (0 << 12)	    // Multicast Offset - bits 47:36
#define RCTL_MO_35 (1 << 12)	    // Multicast Offset - bits 46:35
#define RCTL_MO_34 (2 << 12)	    // Multicast Offset - bits 45:34
#define RCTL_MO_32 (3 << 12)	    // Multicast Offset - bits 43:32
#define RCTL_BAM (1 << 15)	    // Broadcast Accept Mode
#define RCTL_VFE (1 << 18)	    // VLAN Filter Enable
#define RCTL_CFIEN (1 << 19)	    // Canonical Form Indicator Enable
#define RCTL_CFI (1 << 20)	    // Canonical Form Indicator Bit Value
#define RCTL_DPF (1 << 22)	    // Discard Pause Frames
#define RCTL_PMCF (1 << 23)	    // Pass MAC Control Frames
#define RCTL_SECRC (1 << 26)	    // Strip Ethernet CRC

// Buffer Sizes
#define RCTL_BSIZE_256 (3 << 16)
#define RCTL_BSIZE_512 (2 << 16)
#define RCTL_BSIZE_1024 (1 << 16)
#define RCTL_BSIZE_2048 (0 << 16)
#define RCTL_BSIZE_4096 ((3 << 16) | (1 << 25))
#define RCTL_BSIZE_8192 ((2 << 16) | (1 << 25))
#define RCTL_BSIZE_16384 ((1 << 16) | (1 << 25))

// Transmit Command

#define CMD_EOP (1 << 0)  // End of Packet
#define CMD_IFCS (1 << 1) // Insert FCS
#define CMD_IC (1 << 2)	  // Insert Checksum
#define CMD_RS (1 << 3)	  // Report Status
#define CMD_RPS (1 << 4)  // Report Packet Sent
#define CMD_VLE (1 << 6)  // VLAN Packet Enable
#define CMD_IDE (1 << 7)  // Interrupt Delay Enable

// TCTL Register

#define TCTL_EN (1 << 1)      // Transmit Enable
#define TCTL_PSP (1 << 3)     // Pad Short Packets
#define TCTL_CT_SHIFT 4	      // Collision Threshold
#define TCTL_COLD_SHIFT 12    // Collision Distance
#define TCTL_SWXOFF (1 << 22) // Software XOFF Transmission
#define TCTL_RTLC (1 << 24)   // Re-transmit on Late Collision

#define TSTA_DD (1 << 0) // Descriptor Done
#define TSTA_EC (1 << 1) // Excess Collisions
#define TSTA_LC (1 << 2) // Late Collision
#define LSTA_TU (1 << 3) // Transmit Underrun

//
#define REG_ICR 0x00C0

static struct e1000_rx_desc* rx_descs[E1000_NUM_RX_DESC];
static struct e1000_rx_comp rx_comp[E1000_NUM_RX_DESC];
static struct e1000_tx_desc* tx_descs[E1000_NUM_TX_DESC];

static volatile int setup_tx_done = 0;

// buffer pool
static struct rx_buffer_pool rx_buffer_pool;

struct rx_buf_lookup_entry {
	uint8_t* vaddr;
	uint64_t paddr;
};
static rx_buf_lookup_entry g_buf_lookup[BUFFER_POOL_SIZE];
static uint32_t g_buf_lookup_count = 0;

void E1000Module::write(uint16_t p_address, uint32_t p_value) {
	volatile uint32_t* addr =
		(volatile uint32_t*) (device->bar[0].address + p_address);
	*addr = p_value;
}

uint32_t E1000Module::read(uint16_t p_address) {
	return *((volatile uint32_t*) (device->bar[0].address + p_address));
}

boolean_t E1000Module::detectEeprom() {
	uint32_t val = 0;
	write(REG_EEPROM, 0x1);

	for (int i = 0; i < 1000 && !eerprom_exists; i++) {
		val = read(REG_EEPROM);
		// if (val & 0x10)
		if (val & 0b10)
			eerprom_exists = true;
		else
			eerprom_exists = false;
	}
	return eerprom_exists;
}

uint32_t E1000Module::readEeprom(uint32_t addr) {
	uint16_t data = 0;
	uint32_t tmp = 0;
	if (eerprom_exists) {
		write(REG_EEPROM, (1) | ((uint32_t) (addr) << 8));
		while (!((tmp = read(REG_EEPROM)) & (1 << 4)))
			;
	} else {
		write(REG_EEPROM, (1) | ((uint32_t) (addr) << 2));
		while (!((tmp = read(REG_EEPROM)) & (1 << 1)))
			;
	}
	data = (uint16_t) ((tmp >> 16) & 0xFFFF);
	return data;
}

static const char hexmap[] = "0123456789ABCDEF";

boolean_t E1000Module::syncMacAddress() {
	// TODO: detect eeprom dulu

	uint32_t ral = read(0x5400);
	uint32_t rah = read(0x5404);

	// Cek bit Address Valid (Bit 31) di RAH
	// Jika hardware mendukung AV bit, pastikan bit tersebut menyala
	if ((rah & 0x80000000) == 0 && ral == 0) {
		log("E1000", "MAC Address tidak valid (AV bit 0) atau kosong!");
		return false;
	}

	// aktifkan bit AV (address valid)
	rah |= (1u << 31);
	write(0x5404, rah);
	write(0x5400, ral);

	// Masking rah dengan 0xFFFF untuk membuang bit status seperti AV
	// agar tidak ikut masuk ke perhitungan MAC
	mac_addr[0] = ral & 0xFF;
	mac_addr[1] = (ral >> 8) & 0xFF;
	mac_addr[2] = (ral >> 16) & 0xFF;
	mac_addr[3] = (ral >> 24) & 0xFF;
	mac_addr[4] = (rah & 0xFFFF) & 0xFF;
	mac_addr[5] = ((rah & 0xFFFF) >> 8) & 0xFF;

	mac_ready = true;

	char outc[18] = {0};
	uint8_t* out = (uint8_t*) outc;
	for (int i = 0; i < 6; i++) {
		uint8_t byte = mac_addr[i];
		*out++ = hexmap[(byte >> 4) & 0x0F]; // nibble tinggi
		*out++ = hexmap[byte & 0x0F];	     // nibble rendah
		if (i != 5)
			*out++ = ':'; // tambahkan pemisah
	}
	*out = '\0';
	log("E1000", "MAC terbaca dari MMIO: %s", outc);

	return true;
}

void E1000Module::enableInterrupt() {
	write(REG_IMASK, 0);
	read(REG_ICR);

	// msix
	// write(REG_IMASK, IMS_LSC);

	// msi
	// write(REG_IMASK, IMS_RXT0 | IMS_RXDMT0 | IMS_RXO | IMS_LSC);
	write(REG_IMASK, IMS_RXQ0 | IMS_TXQ0 | IMS_OTHER);
	read(REG_ICR); // flush ICR
}

void E1000Module::disableInterrupt() {
	write(REG_IMASK, 0x1F6DC);
	write(REG_IMASK, 0xff);
	read(0xc0);
}
static bool pool_pop(struct rx_buffer* out) {
	uint32_t h = __atomic_load_n(&rx_buffer_pool.head, __ATOMIC_RELAXED);
	uint32_t t = __atomic_load_n(&rx_buffer_pool.tail, __ATOMIC_ACQUIRE);

	if (h == t)
		return false; // kosong

	// Data dijamin sudah ada karena tail baru di-increment SETELAH push
	*out = rx_buffer_pool.buffers[h & BUFFER_POOL_MASK];

	// head++ adalah sinyal ke producer bahwa slot ini bisa dipakai lagi
	__atomic_store_n(&rx_buffer_pool.head, h + 1, __ATOMIC_RELEASE);
	return true;
}

void E1000Module::initReceiverX() {

	struct e1000_rx_desc* descs;

	// untuk descriptor
	uintptr_t paddr = 0;
	auto ptr = (uint8_t*) (IOUtils::DMAAlloc(
		sizeof(struct e1000_rx_desc) * E1000_NUM_RX_DESC + 16, &paddr));

	// alloc untuk buffer pool, ini tidak akan di free

	for (int j = 0; j < BUFFER_POOL_SIZE; j += 2) {
		uintptr_t _paddr = 0;
		auto _ptr = (uint8_t*) (IOUtils::DMAAlloc(4096, &_paddr));

		rx_buffer_pool.buffers[j].vaddr = _ptr;
		rx_buffer_pool.buffers[j].paddr = _paddr;

		rx_buffer_pool.buffers[j + 1].vaddr = _ptr + 2048;
		rx_buffer_pool.buffers[j + 1].paddr = _paddr + 2048;

		g_buf_lookup[g_buf_lookup_count++] = {_ptr, _paddr};
		g_buf_lookup[g_buf_lookup_count++] = {_ptr + 2048,
						      _paddr + 2048};
	}
	rx_buffer_pool.head = 0;
	rx_buffer_pool.tail = BUFFER_POOL_SIZE;

	descs = (struct e1000_rx_desc*) ptr;
	for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
		rx_descs[i] =
			(struct e1000_rx_desc*) ((uintptr_t) descs + i * 16);

		// ambil dari from buffer pool
		struct rx_buffer buf;
		if (!pool_pop(&buf)) {
			// Seharusnya tidak terjadi — pool baru diisi BUFFER_POOL_SIZE slot
			log(mod, "initReceiverX: pool empty saat init! (BUG)");
			continue;
		}

		rx_comp[i].paddr = buf.paddr;
		rx_comp[i].addr = (uint64_t) buf.vaddr;

		// bagian ini DMA (harus phys addr)
		rx_descs[i]->addr = (uint64_t) buf.paddr;
		rx_descs[i]->status = 0;
	}

	write(REG_RXDESCLO, (uint32_t) ((uint64_t) paddr & 0xFFFFFFFF)); // low
	write(REG_RXDESCHI, (uint32_t) ((uint64_t) paddr >> 32));	 // high

	write(REG_RXDESCLEN, E1000_NUM_RX_DESC * 16);

	write(REG_RXDESCHEAD, 0);
	write(REG_RXDESCTAIL, E1000_NUM_RX_DESC - 1);

	rx_cur = 0;

	write(REG_RCTRL, RCTL_EN | RCTL_BAM | RCTL_SECRC | RCTL_BSIZE_2048);

	// write(REG_RCTRL, RCTL_EN | RCTL_BAM | RCTL_SECRC | RCTL_BSIZE_2048
	// 			 | RCTL_UPE | RCTL_MPE);

	// Tuning untuk ping -f:
	// RDTR: delay timer — tunggu N usec setelah paket pertama sebelum interrupt
	// RADV: absolute timer — paksa interrupt setelah N usec meski paket masih datang

	write(REG_RDTR, 0); // Matikan delay timer — langsung interrupt
	write(REG_RADV, 0); // Max tunggu 256 usec sebelum paksa flush
	// write(REG_RADV, 256); // Max tunggu 256 usec sebelum paksa flush

	// RXDCTL: tuning threshold descriptor
	// bit[0:7]  = PTHRESH: pre-fetch threshold
	// bit[8:15] = HTHRESH: host threshold
	// bit[16:23]= WTHRESH: writeback threshold
	// write(REG_RXDCTL, (1 << 25) | (8 << 16) | (4 << 8) | 4);
	//                 granularity   WTHRESH      HTHRESH   PTHRESH

	write(REG_RXDCTL, (1 << 25));
	log(mod, "Receiver initialized");
}

void E1000Module::initTransmitterX() {
	struct e1000_tx_desc* descs;

	uintptr_t paddr = 0;
	uintptr_t ptr = (uintptr_t) (IOUtils::DMAAlloc(
		sizeof(struct e1000_tx_desc) * E1000_NUM_TX_DESC + 16, &paddr));
	log("E1000", "tx_desc addr 0x%x (0x%x)", paddr, ptr);

	descs = (struct e1000_tx_desc*) ptr;
	serial2_printf("descs at 0x%x\n", descs);
	for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
		tx_descs[i] = (struct e1000_tx_desc*) &descs[i];
		tx_descs[i]->addr = 0;
		tx_descs[i]->cmd = 0;
		tx_descs[i]->status = TSTA_DD;
	}

	write(REG_TXDESCHI, (uint32_t) ((uint64_t) paddr >> 32));
	write(REG_TXDESCLO, (uint32_t) ((uint64_t) paddr & 0xFFFFFFFF));

	// now setup total length of descriptors
	write(REG_TXDESCLEN, E1000_NUM_TX_DESC * 16);

	// setup numbers
	write(REG_TXDESCHEAD, 0);
	write(REG_TXDESCTAIL, 0);
	tx_cur = 0;
	write(REG_TCTRL, TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT)
				 | (64 << TCTL_COLD_SHIFT) | TCTL_RTLC);

	write(REG_TCTRL, 0b0110000000000111111000011111010);
	write(REG_TIPG, 0x0060200A);

	__atomic_store_n(&setup_tx_done, 1, __ATOMIC_RELEASE);
	log(mod, "Transmitter initialized");
}

int E1000Module::sendPacket(const struct data_template data[], size_t count) {
	int setup_done = __atomic_load_n(&setup_tx_done, __ATOMIC_ACQUIRE);
	if (setup_done == 0) {
		serial2_printf("wait tx setup done..\n");
		while (!__atomic_load_n(&setup_tx_done, __ATOMIC_ACQUIRE)) {
			asm volatile("pause");
		}
	}

	struct data_template latest_item = data[0];

	uint8_t last_cur = tx_cur;
	for (size_t i = 0; i < count; i++) {
		latest_item = data[i];
		tx_descs[tx_cur]->addr = (uint64_t) latest_item.buffer;
		tx_descs[tx_cur]->length = latest_item.len;
		tx_descs[tx_cur]->cmd = CMD_IFCS | CMD_RS;
		if (i == count - 1 && !latest_item.wait_next_data) {
			tx_descs[tx_cur]->cmd |= CMD_EOP;
		}

		tx_descs[tx_cur]->status = 0;
		last_cur = tx_cur;

		tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
		// TODO: handle kalau tx_cur hampir mendekati head jadi ada resiko override head
	}

	bool success = 0;
	if (!latest_item.wait_next_data) {
		__asm__ volatile("mfence" ::: "memory");
		write(REG_TXDESCTAIL, tx_cur);

		success = (tx_descs[last_cur]->status & 0xff) ? 1 : 1;
	}
	return success;
}

void E1000Module::linkup() {
	uint32_t val = read(REG_CTRL);

	val |= (1 << 6); // SLU (Set Link Up)
	val |= (1 << 6); // SLU
	val |= (1 << 5); // ASDE (auto speed detect enable)
	val |= (1 << 3); // FD (full duplex)

	write(REG_CTRL, val);
}

#define ICR_TXDW (1 << 0)     // Transmit Descriptor Written Back
#define ICR_TXQE (1 << 1)     // Transmit Queue Empty
#define ICR_LSC (1 << 2)      // Link Status Change
#define ICR_RXSEQ (1 << 3)    // Receive Sequence Error
#define ICR_RXDMT0 (1 << 4)   // RX Descriptor Minimum Threshold
#define ICR_RXO (1 << 6)      // Receiver Overrun
#define ICR_RXT0 (1 << 7)     // RX Timer Interrupt
#define ICR_MDAC (1 << 9)     // MDIO Access Complete
#define ICR_RXCFG (1 << 10)   // RX /C/ ordered sets detected
#define ICR_GPI_EN0 (1 << 11) // General Purpose Interrupt 0
#define ICR_GPI_EN1 (1 << 12) // General Purpose Interrupt 1
#define ICR_GPI_EN2 (1 << 13) // General Purpose Interrupt 2
#define ICR_GPI_EN3 (1 << 14) // General Purpose Interrupt 3

void E1000Module::fireHandler() {
	// log("E100 IRQ", "fire");
	E1000Module* module = E1000Module::getInstance();
	if (!module)
		return;

	// untuk msi-x langsung
	module->receiveHandle();

	// check icr
	uint32_t status = module->read(REG_ICR);

	// if (!(status & (IMS_RXT0 | IMS_RXDMT0 | IMS_RXO | IMS_LSC)))
	// 	return;

	if (status & 0x04) {
		log("E100 IRQ", "link up");
		module->linkup();
	}

	if (status & (1 << 20)) {
		module->receiveHandle();
	}
	// Catatan penting MSI-X: Karena fitur EIAC (Auto-Clear) aktif,
	// terkadang hardware membersihkan ICR lebih cepat dari CPU membacanya.
	// Jika kondisinya begitu, kamu bisa memaksa panggil receiveHandle()
	// jika modulmu mencatat current_irq_mode == INT_MSIX.

	if (status & 0x80) {
		// RXT0: paket masuk normal
		module->receiveHandle();
	}
	if (status & 0x40) {
		// RXO: RX overrun — ring penuh, paksa drain
		log("E100 IRQ", "RX overrun!");
		module->receiveHandle();
	}
}

void E1000Module::receiveHandle() {
	int processed = 0;
	uint16_t last_tail = (uint16_t) -1;

	while (rx_descs[rx_cur]->status & 0x1) {
		struct rx_buffer new_buf;

		if (!pool_pop(&new_buf)) {
			rx_descs[rx_cur]->addr =
				rx_comp[rx_cur].paddr; // re-post lama
			rx_descs[rx_cur]->status = 0;
			last_tail = rx_cur;
			rx_cur = (rx_cur + 1) & E1000_NUM_RX_MASK;

			if (++processed % 16 == 0)
				write(REG_RXDESCTAIL, last_tail);
			continue; // drop
		}

		uint8_t* old_vaddr = (uint8_t*) rx_comp[rx_cur].addr;
		uint16_t pkt_size = rx_descs[rx_cur]->length;

		rx_comp[rx_cur].paddr = new_buf.paddr;
		rx_comp[rx_cur].addr = (uint64_t) new_buf.vaddr;
		rx_descs[rx_cur]->addr = new_buf.paddr;
		rx_descs[rx_cur]->status = 0;
		last_tail = rx_cur;
		rx_cur = (rx_cur + 1) & E1000_NUM_RX_MASK;

		ioforge_nic_rx(nic, old_vaddr, pkt_size, (last_tail));

		if (++processed % 16 == 0)
			write(REG_RXDESCTAIL, last_tail);
	}

	if (last_tail != (uint16_t) -1 && processed % 16 != 0)
		write(REG_RXDESCTAIL, last_tail);
}

static void pool_push(uint8_t* vaddr, uint64_t paddr) {
	uint32_t h = __atomic_load_n(&rx_buffer_pool.head, __ATOMIC_ACQUIRE);
	uint32_t t = __atomic_load_n(&rx_buffer_pool.tail, __ATOMIC_RELAXED);

	if (t - h >= BUFFER_POOL_SIZE) {
		serial2_printf("[E1000] pool_push: FULL, drop vaddr=%p\n",
			       vaddr);
		return;
	}

	uint32_t slot = t & BUFFER_POOL_MASK;
	rx_buffer_pool.buffers[slot].vaddr = vaddr;
	rx_buffer_pool.buffers[slot].paddr = paddr;

	__atomic_store_n(&rx_buffer_pool.tail, t + 1, __ATOMIC_RELEASE);
}

void E1000Module::storeBufferToPool(int /*rx_id*/, void* vaddr) {
	uint64_t paddr = 0;
	bool found = false;
	for (uint32_t i = 0; i < g_buf_lookup_count; i++) {
		if (g_buf_lookup[i].vaddr == (uint8_t*) vaddr) {
			paddr = g_buf_lookup[i].paddr;
			found = true;
			break;
		}
	}

	if (!found) {
		serial2_printf(
			"[E1000] storeBufferToPool: vaddr %p tidak dikenal!\n",
			vaddr);
		return;
	}

	pool_push((uint8_t*) vaddr, paddr);
}

int E1000Module::getMacAddress(uint8_t mac[6]) {
	// wait until ready
	while (!mac_ready)
		asm volatile("pause");

	for (int i = 0; i < 6; i++)
		mac[i] = mac_addr[i];

	return 1;
}