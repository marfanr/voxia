#include <firmw/display/vga.h>
#include <firmw/pci/pci.h>
#include <libk/io.h>
#include <libk/serial.h>
#include <libk/type.h>
pci_device_t vga_dev, agp_dev;

void vga_setup() {
  for (int i = 0; i < 32; i++) {
    // TODO: find with propper way
    uint8_t subclass = pci_devices[i].subclass;
    uint8_t class = pci_devices[i].class;
    if (subclass == 0x80 && class == 0x3) {
      // EHCI (Not properly implemented but ok for
      //     now)
      vga_dev = pci_devices[i];
      serial_send_string("\nFound VGA device\n");
      break;
    }
  }

  // print combine bar 0 and bar 1 into 1 addr
  uint32_t gmr = vga_dev.bar[0];
  serial_send_string("GMR : ");
  serial_send_number(gmr, 16);
  serial_send_string("\n");

  // print command address
  serial_send_string("Command : ");
  serial_send_number(vga_dev.command, 16);
  serial_send_string("\n");

  // draw a box in the gmr addr
  for (int i = 0; i < 100; i++) {
    for (int j = 0; j < 100; j++) {
      uint32_t *pixel = (uint32_t *)(gmr + (i * 100 + j) * 4);
      *pixel = 0x00FF0000;
    }
  }

  // TODO: move it to another fil to make more propperly
  for (int i = 0; i < 32; i++) {
    // TODO: find with propper way
    uint8_t subclass = pci_devices[i].subclass;
    uint8_t class = pci_devices[i].class;
    if (subclass == 0x80 && class == 0x3) {
      // EHCI (Not properly implemented but ok for
      //     now)
      agp_dev = pci_devices[i];
      serial_send_string("\nFound Accelerated Graphics Port (AGP) device\n");

      break;
    }
  }

  serial_send_string("AGP Command : 0x");
  serial_send_number(agp_dev.command, 16);
  serial_send_string("\n");

  uint32_t agpcmd = agp_dev.command;
}