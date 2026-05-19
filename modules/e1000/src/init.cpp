#include "e1000/e1000.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.hpp"
#include "ioforge/ioforge_pci.h"
#include "ioforge/ioforge_pci.hpp"
#include <ioforge/ioforge.hpp>

IoForgeModuleConstructor(E1000Module);

E1000Module::E1000Module() : IOforgePCI("E1000 Ethernet") {
}

void E1000Module::unload() {
}

E1000Module* E1000Module::getInstance() {
	return &instance;
}

extern "C" void fireHandler() {
	log("E1000 IRQ", "ok");
}

#define IMS_RXQ0 (1 << 20)  // Bit 20: Receive Queue 0
#define IMS_TXQ0 (1 << 22)  // Bit 22: Transmit Queue 0
#define IMS_OTHER (1 << 24) // Bit 24: Other Causes (LSC, dll)

void E1000Module::load() {
	device = findDevice(0x8086, 0x100C);
	device = findDevice(0x8086, 0x10D3);
	if (!device) {
		log(mod, "Device not found");
		return;
	}
	log(mod, "Device found bar0 at 0x%x", device->bar[0].address);
	uint32_t status = read(0x0008); // STATUS register
	log(mod, "STATUS: 0x%x", status);

	uint32_t ctrl = read(0x0000);	 // CTRL
	write(0x0000, ctrl | (1 << 26)); // set RST bit
	// tunggu reset selesai
	for (volatile int i = 0; i < 10000; i++)
		;
	while (read(0x0000) & (1 << 26))
		;

	if (detectEeprom()) {
		log(mod, "Eprom found");
	} else {
		log(mod, "Eprom not found");
	}

	syncMacAddress();

	linkup();

	for (int i = 0; i < 0x80; i++)
		write(0x5200 + i * 4, 0);

	initReceiverX();
	initTransmitterX();

	serial2_printf("E1000 interrupt line: %d (%d)\n",
		       device->interrupt_line);

	uint16_t msix_cap = pci_cap_find_msix(device);
	uint16_t msi_cap = pci_cap_find_msi(device);

	if (msix_cap) {
		log(mod, "MSI-X Available at cap : 0x%x", msix_cap);
		auto cpu = get_current_core_cpuid();
		auto irq = IOUtils::irq_alloc_entry();
		IOUtils::irq_register(irq, (void*) E1000Module::fireHandler);
		pci_enable_msix(device, irq, cpu, msix_cap);
		// current_irq_mode = INT_MSIX;

		// 1. Konfigurasi CTRL_EXT (0x0018) untuk MSI-X
		// Bit 24 (EIAME), Bit 27 (IAME), Bit 31 (PBA_CLR)
		uint32_t ctrl_ext = read(0x0018);
		ctrl_ext |= (1 << 24) | (1 << 27) | (1u << 31);
		write(0x0018, ctrl_ext);

		// 2. IVAR (0x00E4): Mapping RXQ0 dan TXQ0 ke Vector 0
		// FORMAT 82574L YANG BENAR: 4-bit per antrean
		uint32_t ivar = 0;
		// RXQ0 -> Vector 0 (Bit 3 = Valid, Bit 2:0 = Vector 0)
		ivar |= (1 << 3) | 0;
		// TXQ0 -> Vector 0 (Bit 11 = Valid, Bit 10:8 = Vector 0)
		ivar |= (1 << 11) | (0 << 8);
		write(0x00E4, ivar); // Hasilnya akan menjadi 0x0808

		// IVAR_MISC: Other causes (LSC, dll) -> Vector 0
		// (Bit 3 = Valid, Bit 2:0 = Vector 0)
		write(0x00E8, (1 << 3) | 0); // Hasilnya akan menjadi 0x0008

		write(0x000DC, IMS_RXQ0 | IMS_TXQ0 | IMS_OTHER);

		read(0x01580);
		read(0x000C0);

		msix = 1;
	}

	else if (msi_cap) {
		log(mod, "MSI Available at 0x%x", msi_cap);
		auto irq = IOUtils::irq_alloc_entry();
		auto cpu = get_current_core_cpuid();
		IOUtils::irq_register(irq, (void*) E1000Module::fireHandler);
		pci_enable_msi(device, irq, cpu, msi_cap);
		msix = 0;
	}

	else if (device->interrupt_line) {
		log(mod, "Using Legacy IRQ");

		auto irq = IOUtils::irq_alloc_entry();
		IOUtils::isr_map(device->interrupt_line, irq);
		IOUtils::irq_register(irq, (void*) E1000Module::fireHandler);
		msix = 0;
	}

	enableInterrupt();
	log(mod, "Successfully Initialized Module");
}

extern "C" int
E1000SendPacketCWrapper(const struct data_template data[], size_t count) {
	int a = instance.sendPacket(data, count);
	return a;
}
extern "C" int E1000GetMacAddressCWrapper(uint8_t mac[6]) {
	return instance.getMacAddress(mac);
}

// extern "C" int E1000ReceivePacketCWraper(void** buffer, size_t* size) {
// 	return instance.receivePacket(buffer, size);
// }

extern "C" void E1000StoreBufferToPoolCWrapper(int rx_id, void* vaddr) {
	instance.storeBufferToPool(rx_id, vaddr);
}

__attribute__((constructor)) static void e100_constructor() {
	ioforge_nic_service* nic =
		(ioforge_nic_service*) IOForge::IOUtils::alloc(
			sizeof(ioforge_nic_service));
	log("E1000", "nic 0x%x", nic);

	const char* service_name = "E1000";
	IOForge::IOUtils::strcopy((char*) nic->service.name,
				  (char*) service_name);
	nic->ops.send = E1000SendPacketCWrapper;
	// nic->ops.receive = E1000ReceivePacketCWraper;
	nic->ops.get_mac_address = E1000GetMacAddressCWrapper;
	nic->ops.storeBufferToPool = E1000StoreBufferToPoolCWrapper;

	nic->pq_head = nic->pq_tail = 0;

	E1000Module::getInstance()->setNIC(nic);

	// IoForgeNIC::create(nic);
	serial2_printf("[E1000] Constructor selesai dijalankan\n");
}
