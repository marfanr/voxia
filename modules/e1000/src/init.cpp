#include "e1000/e1000.hpp"
#include "ioforge/ioforge.h"
#include "ioforge/ioforge_nic.hpp"
#include "ioforge/ioforge_pci.hpp"
#include <ioforge/ioforge.hpp>

IoForgeModuleConstructor(E1000Module);

E1000Module::E1000Module() : IOforgePCI("E1000 Ethernet") {}

void E1000Module::unload() {}

E1000Module* E1000Module::getInstance() { return &instance; }

extern "C" void fireHandler() { log("E1000 IRQ", "ok"); }

void E1000Module::load() {
	// device = findDevice(0x8086, 0x100C);
	device = findDevice(0x8086, 0x10D3);
	if (!device) {
		log(mod, "Device not found");
		return;
	}
	log(mod, "Device found bar0 at 0x%x", device->bar[0].address);
	uint32_t status = read(0x0008); // STATUS register
	log(mod, "STATUS: 0x%x", status);

	uint32_t ctrl = read(0x0000);    // CTRL
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

	enableInterrupt();
	initReceiverX();
	initTransmitterX();

	uint32_t tctl = read(0x0400);
	serial2_printf("TCTL = 0x%x\n", tctl);

	IOUtils::isr_map(10, 0x55);
	IOUtils::irq_register(0x55, (void*)E1000Module::fireHandler);

	log(mod, "Successfully Initialized Module");
}

extern "C" int E1000SendPacketCWrapper(const void* data, size_t len) {
	int a = instance.sendPacket(data, len);
	return a;
}

static int E1000GetMacAddressCWrapper(uint8_t mac[6]) {
	return instance.getMacAddress(mac);
}

static int E1000ReceivePacketCWraper(void** buffer, size_t* size) {
	return instance.receivePacket(buffer, size);
}

__attribute__((constructor)) static void e100_constructor() {
	ioforge_nic_service* nic =
	    (ioforge_nic_service*)IOForge::IOUtils::alloc(
	        sizeof(ioforge_nic_service));
	nic->ops = (ioforge_nic_operation*)IOForge::IOUtils::alloc(
	    sizeof(ioforge_nic_operation));
	const char* service_name = "E1000";
	IOForge::IOUtils::strcopy((char*)nic->service.name,
	                          (char*)service_name);
	nic->ops->send = E1000SendPacketCWrapper;
	nic->ops->receive = E1000ReceivePacketCWraper;
	nic->ops->get_mac_address = E1000GetMacAddressCWrapper;

	IoForgeNIC::create(nic);
	serial2_printf("[E1000] Constructor selesai dijalankan\n");
}
