#include "e1000/e1000.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.h"
#include "type.h"
#include <cstdint>
#include <cstring>

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

static struct e1000_rx_desc* rx_descs[E1000_NUM_RX_DESC];
static struct e1000_rx_comp rx_comp[E1000_NUM_RX_DESC];
static struct e1000_tx_desc* tx_descs[E1000_NUM_TX_DESC];

static volatile int setup_tx_done = 0;

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

	// Masking rah dengan 0xFFFF untuk membuang bit status seperti AV
	// agar tidak ikut masuk ke perhitungan MAC
	mac_addr[0] = ral & 0xFF;
	mac_addr[1] = (ral >> 8) & 0xFF;
	mac_addr[2] = (ral >> 16) & 0xFF;
	mac_addr[3] = (ral >> 24) & 0xFF;
	mac_addr[4] = (rah & 0xFFFF) & 0xFF;
	mac_addr[5] = ((rah & 0xFFFF) >> 8) & 0xFF;

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
	write(REG_IMASK, IMS_RXT0 | IMS_RXDMT0 | IMS_RXO | IMS_LSC);
	read(0x00C0); // flush ICR
}

void E1000Module::disableInterrupt() {
	write(REG_IMASK, 0x1F6DC);
	write(REG_IMASK, 0xff);
	read(0xc0);
}

void E1000Module::initReceiverX() {
	uint8_t* ptr;
	struct e1000_rx_desc* descs;

	uintptr_t paddr;
	ptr = (uint8_t*) (IOUtils::DMAAlloc(
		sizeof(struct e1000_rx_desc) * E1000_NUM_RX_DESC + 16, &paddr));

	descs = (struct e1000_rx_desc*) ptr;
	for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
		rx_descs[i] =
			(struct e1000_rx_desc*) ((uintptr_t) descs + i * 16);
		uintptr_t a_paddr;
		auto a = IOUtils::DMAAlloc(2048 + 16, &a_paddr);
		rx_descs[i]->addr = (uint64_t) a_paddr;
		rx_comp[i].paddr = a_paddr;
		rx_comp[i].addr = (uint64_t) a;
		rx_descs[i]->status = 0;
	}

	write(REG_RXDESCLO, (uint32_t) ((uint64_t) paddr & 0xFFFFFFFF)); // low
	write(REG_RXDESCHI, (uint32_t) ((uint64_t) paddr >> 32));	 // high

	write(REG_RXDESCLEN, E1000_NUM_RX_DESC * 16);

	write(REG_RXDESCHEAD, 0);
	write(REG_RXDESCTAIL, E1000_NUM_RX_DESC - 1);

	rx_cur = 0;

	write(REG_RCTRL, RCTL_EN | RCTL_BAM | RCTL_SECRC | RCTL_BSIZE_2048);
	write(REG_RXDCTL, (1 << 25) | 1);
	write(REG_RDTR, 0);
	write(REG_RADV, 0);

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

	// This line of code overrides the one before it but I left both to
	// highlight that the previous
	// one works with e1000 cards, but for the e1000e cards you should set
	// the TCTRL register as
	// follows. For detailed description of each bit, please refer to the
	// Intel Manual. In the case
	// of I217 and 82577LM packets will not be sent if the TCTRL is not
	// configured using the
	// following bits.
	write(REG_TCTRL, 0b0110000000000111111000011111010);
	write(REG_TIPG, 0x0060200A);

	__atomic_store_n(&setup_tx_done, 1, __ATOMIC_RELEASE);
	log(mod, "Transmitter initialized");
}

int E1000Module::sendPacket(const void* data, size_t len) {
	int setup_done = __atomic_load_n(&setup_tx_done, __ATOMIC_ACQUIRE);
	if (setup_done == 0) {
		serial2_printf("wait tx setup done..\n");
		while (!__atomic_load_n(&setup_tx_done, __ATOMIC_ACQUIRE)) {
			// optional: pause / cpu_relax
		}
	}
	tx_descs[tx_cur]->addr = (uint64_t) data;
	tx_descs[tx_cur]->length = len;
	tx_descs[tx_cur]->cmd = CMD_EOP | CMD_IFCS | CMD_RS;
	tx_descs[tx_cur]->status = 0;
	uint8_t old_cur = tx_cur;
	tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
	write(REG_TXDESCTAIL, tx_cur);

	bool success = 0;
	for (int i = 0; i < 3; i++) {
		if (tx_descs[old_cur]->status & 0xff) {
			success = true;
			break;
		}
		IOUtils::sleep(1000000);
	}

	return success;
}

void E1000Module::linkup() {
	// uint32_t val = read(REG_CTRL);
	// write(REG_CTRL, val | ECTRL_SLU | 2);

	uint32_t val = read(REG_CTRL);

	// val |= (1 << 6); // SLU (Set Link Up)
	val |= (1 << 6); // SLU
	val |= (1 << 5); // ASDE (auto speed detect enable)
	val |= (1 << 3); // FD (full duplex)

	write(REG_CTRL, val);
}

void E1000Module::fireHandler() {
	E1000Module* module = E1000Module::getInstance();
	if (!module)
		return;
	uint32_t status = module->read(0xc0);
	// log("E100 IRQ", "status 0x%x", status);

	if (status & 0x04) {
		log("E100 IRQ", "link up");
		module->linkup();
	} else if (status & 0x10) {
		log("E100 IRQ", "good threshold");
		// good threshold
	} else if (status & 0x80) {
		// log("E100 IRQ", "receive");
		module->receiveHandle();
	} else {
		log("E100 IRQ", "unknown");
	}
}

void E1000Module::receiveHandle() {
	uint16_t old_cur;
	got_packet = false;

	while ((rx_descs[rx_cur]->status & 0x1)) {
		got_packet = true;

		// auto latest_packet_buffer = (uint8_t*) rx_comp[rx_cur].addr;
		// for (int i = 0; i < 112; i++) {
		// 	serial2_printf("0x%x ", latest_packet_buffer[i]);
		// }

		// serial2_printf("\n package size (%d)\n", latest_packet_size);

		auto latest_packet_size = rx_descs[rx_cur]->length;
		kpacket_t packet = {
			.data = (uint8_t*) rx_comp[rx_cur].addr,
			.len = latest_packet_size,
		};

		// send back to kernel
		IOForgeNICRx(nic, &packet);

		// next tail
		rx_descs[rx_cur]->status = 0;

		old_cur = rx_cur;
		rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;
		write(REG_RXDESCTAIL, old_cur);
		write(REG_RDTR, rx_cur);
	}
}

int E1000Module::receivePacket(void** buffer, size_t* size) {
	if (!(rx_descs[last_readed_rx_cur]->status & 0x1)) {
		return 0;
	}

	*buffer = (void*) rx_comp[last_readed_rx_cur].addr;
	*size = rx_descs[last_readed_rx_cur]->length;

	rx_descs[last_readed_rx_cur]->status = 0;
	last_readed_rx_cur++;

	return 1;
}

int E1000Module::getMacAddress(uint8_t mac[6]) {
	for (int i = 0; i < 6; i++)
		mac[i] = mac_addr[i];

	return 1;
}