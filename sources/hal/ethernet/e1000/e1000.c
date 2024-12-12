#include "e1000.h"
#include <firmw/pci/pci.h>
#include <libk/io.h>
#include <libk/net/arp.h>
#include <libk/net/ethernet.h>
#include <libk/net/icmp.h>
#include <libk/net/ip.h>
#include <libk/net/udp.h>
#include <libk/serial.h>
#include <libk/timer.h>
#include <memory/memory_utils.h>
#include <memory/phys_base_allocator.h>

pci_device_t eth_e1000_device;
uint8_t mac[5];

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

#define REG_TCTRL 0x0400
#define REG_TXDESCLO 0x3800
#define REG_TXDESCHI 0x3804
#define REG_TXDESCLEN 0x3808
#define REG_TXDESCHEAD 0x3810
#define REG_TXDESCTAIL 0x3818

#define REG_RDTR 0x2820   // RX Delay Timer Register
#define REG_RXDCTL 0x2828 // RX Descriptor Control
#define REG_RADV 0x282C   // RX Int. Absolute Delay Timer
#define REG_RSRPD 0x2C00  // RX Small Packet Detect Interrupt

#define REG_TIPG 0x0410 // Transmit Inter Packet Gap
#define ECTRL_SLU 0x40  // set link up

#define RCTL_EN (1 << 1)            // Receiver Enable
#define RCTL_SBP (1 << 2)           // Store Bad Packets
#define RCTL_UPE (1 << 3)           // Unicast Promiscuous Enabled
#define RCTL_MPE (1 << 4)           // Multicast Promiscuous Enabled
#define RCTL_LPE (1 << 5)           // Long Packet Reception Enable
#define RCTL_LBM_NONE (0 << 6)      // No Loopback
#define RCTL_LBM_PHY (3 << 6)       // PHY or external SerDesc loopback
#define RTCL_RDMTS_HALF (0 << 8)    // Free Buffer Threshold is 1/2 of RDLEN
#define RTCL_RDMTS_QUARTER (1 << 8) // Free Buffer Threshold is 1/4 of RDLEN
#define RTCL_RDMTS_EIGHTH (2 << 8)  // Free Buffer Threshold is 1/8 of RDLEN
#define RCTL_MO_36 (0 << 12)        // Multicast Offset - bits 47:36
#define RCTL_MO_35 (1 << 12)        // Multicast Offset - bits 46:35
#define RCTL_MO_34 (2 << 12)        // Multicast Offset - bits 45:34
#define RCTL_MO_32 (3 << 12)        // Multicast Offset - bits 43:32
#define RCTL_BAM (1 << 15)          // Broadcast Accept Mode
#define RCTL_VFE (1 << 18)          // VLAN Filter Enable
#define RCTL_CFIEN (1 << 19)        // Canonical Form Indicator Enable
#define RCTL_CFI (1 << 20)          // Canonical Form Indicator Bit Value
#define RCTL_DPF (1 << 22)          // Discard Pause Frames
#define RCTL_PMCF (1 << 23)         // Pass MAC Control Frames
#define RCTL_SECRC (1 << 26)        // Strip Ethernet CRC

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
#define CMD_IC (1 << 2)   // Insert Checksum
#define CMD_RS (1 << 3)   // Report Status
#define CMD_RPS (1 << 4)  // Report Packet Sent
#define CMD_VLE (1 << 6)  // VLAN Packet Enable
#define CMD_IDE (1 << 7)  // Interrupt Delay Enable

// TCTL Register

#define TCTL_EN (1 << 1)      // Transmit Enable
#define TCTL_PSP (1 << 3)     // Pad Short Packets
#define TCTL_CT_SHIFT 4       // Collision Threshold
#define TCTL_COLD_SHIFT 12    // Collision Distance
#define TCTL_SWXOFF (1 << 22) // Software XOFF Transmission
#define TCTL_RTLC (1 << 24)   // Re-transmit on Late Collision

#define TSTA_DD (1 << 0) // Descriptor Done
#define TSTA_EC (1 << 1) // Excess Collisions
#define TSTA_LC (1 << 2) // Late Collision
#define LSTA_TU (1 << 3) // Transmit Underrun

struct e1000_rx_desc *rx_desc[E1000_NUM_RX_DESC];
struct e1000_tx_desc *tx_desc[32];
uint16_t rx_cur = 0;
uint16_t tx_cur = 0;
uint16_t iobar = 0;

uint16_t readEeprom(uint32_t mmbase, uint8_t addr) {
  uint32_t t = 1;
  t |= ((uint32_t)(addr) << 8);
  (*((volatile uint32_t *)(mmbase + 0x14))) = t;
  uint32_t tmp = 0;
  while (!(tmp & (1 << 4)))
    tmp = *((volatile uint32_t *)(mmbase + 0x14));
  return (tmp >> 16);
}
uint32_t mmbar = 0;

void e1000_linkup() {
  uint32_t val;
  val = (*((volatile uint32_t *)(mmbar + REG_CTRL)));
  *((volatile uint32_t *)(mmbar + REG_CTRL)) = val | ECTRL_SLU;
}

void writeCommand(uint16_t p_address, uint32_t p_value) {
  (*((volatile uint32_t *)(mmbar + p_address))) = p_value;
}

uint32_t readCommand(uint16_t p_address) {
  return (*((volatile uint32_t *)(mmbar + p_address)));
}

int sendPacket(const void *p_data, uint16_t p_len) {
  tx_desc[tx_cur]->addr = (uint64_t)p_data;
  tx_desc[tx_cur]->length = p_len;
  tx_desc[tx_cur]->cmd = CMD_EOP | CMD_IFCS | CMD_RS;
  tx_desc[tx_cur]->status = 0;
  uint8_t old_cur = tx_cur;
  tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;
  writeCommand(REG_TXDESCTAIL, tx_cur);
  while (!(tx_desc[old_cur]->status & 0xff))
    ;
  serial_trace("packet sent\n");
  return 0;
}

void memcopy(void *dest, void *src, uint32_t size) {
  uint8_t *d = (uint8_t *)dest;
  uint8_t *s = (uint8_t *)src;
  for (uint32_t i = 0; i < size; i++) {
    d[i] = s[i];
  }
}

uint16_t switch_endian16(uint16_t nb) { return (nb >> 8) | (nb << 8); }

void txinit() {
  uint8_t *ptr;
  struct e1000_tx_desc *descs;
  // Allocate buffer for receive descriptors. For simplicity, in my case
  // khmalloc returns a virtual address that is identical to it physical mapped
  // address. In your case you should handle virtual and physical addresses as
  // the addresses passed to the NIC should be physical ones
  ptr = (uint8_t *)(VIRT2PHYS(phys_base_alloc(
      1 + (sizeof(struct e1000_tx_desc) * E1000_NUM_TX_DESC + 16) / 4096)));
  // serial_trace("\ntx ptr : 0x%x\n", ptr);

  descs = (struct e1000_tx_desc *)ptr;
  for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
    tx_desc[i] = (struct e1000_tx_desc *)((uint8_t *)descs + i * 16);
    // serial_trace(" tx desc %d : 0x%x\n", i, tx_desc[i]);
    tx_desc[i]->addr = 0;
    tx_desc[i]->cmd = 0;
    tx_desc[i]->status = TSTA_DD;
  }

  writeCommand(REG_TXDESCHI, 0);
  writeCommand(REG_TXDESCLO, (uint32_t)(ptr));

  // now setup total length of descriptors
  writeCommand(REG_TXDESCLEN, E1000_NUM_TX_DESC * 16);

  // setup numbers
  writeCommand(REG_TXDESCHEAD, 0);
  writeCommand(REG_TXDESCTAIL, 0);
  tx_cur = 0;
  writeCommand(REG_TCTRL, TCTL_EN | TCTL_PSP | (15 << TCTL_CT_SHIFT) |
                              (64 << TCTL_COLD_SHIFT) | TCTL_RTLC);

  // This line of code overrides the one before it but I left both to highlight
  // that the previous one works with e1000 cards, but for the e1000e cards you
  // should set the TCTRL register as follows. For detailed description of each
  // bit, please refer to the Intel Manual. In the case of I217 and 82577LM
  // packets will not be sent if the TCTRL is not configured using the following
  // bits.
  writeCommand(REG_TCTRL, 0b0110000000000111111000011111010);
  writeCommand(REG_TIPG, 0x0060200A);
}

void rxinit() {
  uintptr_t ptr;
  struct e1000_rx_desc *descs;

  // Allocate buffer for receive descriptors. For simplicity, in my case
  // khmalloc returns a virtual address that is identical to it physical mapped
  // address. In your case you should handle virtual and physical addresses as
  // the addresses passed to the NIC should be physical ones

  // TODO : use propper virtual adress

  ptr = (uintptr_t)VIRT2PHYS((phys_base_alloc(
      1 + (sizeof(struct e1000_rx_desc) * E1000_NUM_RX_DESC + 16) / 4096)));
  memset(ptr, 0,
         4096 + (sizeof(struct e1000_rx_desc) * E1000_NUM_RX_DESC + 16) / 4096);
  if (ptr % 16 != 0)
    ptr = (ptr + 16) - (ptr % 16);
  serial_trace("\nrx ptr : 0x%x\n", ptr);
  descs = (struct e1000_rx_desc *)ptr;
  for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
    rx_desc[i] = (struct e1000_rx_desc *)((uintptr_t)descs + i * 16);
    // serial_trace(" rx desc %d : 0x%x\n", i, rx_desc[i]);
    rx_desc[i]->addr =
        (uint64_t)VIRT2PHYS(phys_base_alloc(2 + ((8192 + 16) / 4096)));
    // serial_trace(" rx addr %d : 0x%x\n", i, rx_desc[i]->addr);
    rx_desc[i]->status = 0;
  }

  // writeCommand(REG_TXDESCLO, (uint32_t)((uint64_t)ptr >> 32));
  // writeCommand(REG_TXDESCHI, (uint32_t)((uint64_t)ptr & 0xFFFFFFFF));

  // serial_trace("rxdeschi 0x%x\n", (uint32_t)((uint64_t)ptr >> 32));

  writeCommand(REG_RXDESCLO, (uint32_t)(ptr));
  // serial_trace("rxdes 0x%x\n", (uint32_t)(ptr & 0xFFFFFFFF));
  // writeCommand(REG_RXDESCHI, (uint32_t)(ptr >> 32));

  writeCommand(REG_RXDESCLEN, (E1000_NUM_RX_DESC * 16));

  writeCommand(0xc4, 100);

  writeCommand(REG_RXDESCHEAD, 0);
  writeCommand(REG_RXDESCTAIL, E1000_NUM_RX_DESC - 1);
  rx_cur = 0;

  writeCommand(REG_RCTRL, (2 << 16) | (1 << 25) | (1 << 26) | (1 << 15) |
                              (1 << 5) | (1 << 8) | (1 << 4) | (1 << 3) |
                              (1 << 2) | (1 << 18));
}

void eth_e1000_init() {
  for (int i = 0; i < 32; i++) {
    // TODO: find with propper way
    uint8_t subclass = pci_devices[i].subclass;
    uint8_t class = pci_devices[i].class;
    if (subclass == 0x0 && class == 0x2) {
      serial_send_string("\nFound Intel 82551 Ethernet card device\n");
      eth_e1000_device = pci_devices[i];
      // break;
    }
  }
  serial_trace("vendor id : %x\n", eth_e1000_device.vendorID);
  serial_trace("device id : %x\n", eth_e1000_device.deviceID);

  mmbar = (uint32_t)(eth_e1000_device.bar[0]);
  serial_trace("mmbar : 0x%x\n", mmbar);
  // check is 32 bit or 64 bit
  serial_trace("bar 0 type : %b\n", mmbar & 0b110);
  if (eth_e1000_device.bar[0] & 0x6) {
    serial_trace("64 bit\n");
  }

  iobar = (uint16_t)((eth_e1000_device.bar[1] & ~1));
  serial_trace("iobar : 0x%x\n", iobar);

  uint8_t *flashbar = (uint8_t *)(eth_e1000_device.bar[6] & 0xFFFFFFFC);
  serial_trace("flashbar : 0x%x\n", flashbar);

  // TODO: enabling bus mastering
  enable_bus_mastering(eth_e1000_device);

  uint32_t *eeprom = (uint32_t *)(mmbar + 0x14);
  *eeprom |= (1);
  int eerprom_exists = 0;
  for (int i = 0; i < 1000 && !eerprom_exists; i++) {
    uint32_t val = (uint32_t *)(mmbar + 0x14);
    if (val & 0x10)
      eerprom_exists = 1;
    else
      eerprom_exists = 0;
  }
  serial_trace("eeprom exists : %d\n", eerprom_exists);
  uint32_t temp = readEeprom(mmbar, 0);
  mac[0] = temp & 0xff;
  mac[1] = temp >> 8;
  temp = readEeprom(mmbar, 1);
  mac[2] = temp & 0xff;
  mac[3] = temp >> 8;
  temp = readEeprom(mmbar, 2);
  mac[4] = temp & 0xff;
  mac[5] = temp >> 8;

  char *mac_str = "00:00:00:00:00:00";
  for (int i = 0; i < 6; i++) {
    mac_str[i * 3] = "0123456789ABCDEF"[mac[i] >> 4];
    mac_str[i * 3 + 1] = "0123456789ABCDEF"[mac[i] & 0xf];
  }
  serial_trace("mac address : %s\n", mac_str);

  // initilize
  e1000_linkup();
  for (int i = 0; i < 0x80; i++)
    writeCommand(0x5200 + i * 4, 0);
  // writeCommand(0x5200 + i*4, 0);
  writeCommand(REG_IMASK, 0x1F6DC);
  writeCommand(REG_IMASK, 0xff & ~4);
  readCommand(0xc0);

  serial_trace("device interrupt is ready\n");

  rxinit();
  serial_trace("device rx is ready\n");

  txinit();
  serial_trace("device tx is ready\n\n");

  // char *d = "hello world";
  // int i = sendPacket(d, 10);
  // serial_trace("sud mesage sucess : %d\n\n", i);

  uint32_t flags = readCommand(REG_RCTRL);
  writeCommand(REG_RCTRL, flags | 0x2);

  usleep(500);

  // test send request
  ethernet_header_t eth_req;
  eth_req.type = switch_endian16(0x0806);
  for (int i = 0; i < 6; i++) {
    eth_req.src[i] = switch_endian16(mac[i]) >> 8;
    eth_req.dest[i] = 0xff;
  }
  arp_header_t arp_req;
  arp_req.htype = switch_endian16(0x0001);
  arp_req.ptype = switch_endian16(0x0800);
  arp_req.hlen = 6;
  arp_req.plen = 4;
  arp_req.op = switch_endian16(0x0001);

  uint8_t *buf =
      (uint8_t *)phys_base_alloc(1 + (sizeof(ethernet_header_t) + 16) / 4096);
  memcopy(buf, &eth_req, sizeof(ethernet_header_t));
  memcopy(buf + sizeof(ethernet_header_t), &arp_req, sizeof(arp_header_t));

  uint8_t mac_endian[6];
  for (int i = 0; i < 6; i++) {
    mac_endian[i] = switch_endian16(mac[i]) >> 8;
    // serial_trace("%x:", mac_endian[i]);
  }

  memcopy(buf + sizeof(ethernet_header_t) + sizeof(arp_header_t), mac_endian,
          6);
  memcopy(buf + sizeof(ethernet_header_t) + sizeof(arp_header_t) + 6,
          (uint8_t *)"\xC0\xA8\x01\x01", 4);

  uint8_t mac_target[6];
  for (int i = 0; i < 6; i++) {
    mac_target[i] = switch_endian16(0xFF) >> 8;
    // serial_trace("%x:", mac_endian[i]);
  }
  memcopy(buf + sizeof(ethernet_header_t) + sizeof(arp_header_t) + 6 + 4,
          mac_target, 6);
  uint8_t ip_to_send[4] = {192, 168, 1, 30};
  for (int i = 0; i < 4; i++) {
    ip_to_send[i] = switch_endian16(ip_to_send[i]) >> 8;
  }
  memcopy(buf + sizeof(ethernet_header_t) + sizeof(arp_header_t) + 6 + 4 + 6,
          ip_to_send, 4);

  sendPacket(VIRT2PHYS(buf),
             sizeof(ethernet_header_t) + sizeof(arp_header_t) + 6 + 4 + 6 + 4);
}

void e1000_irq() {
  uint32_t status = readCommand(0xc0);

  if (status & 0x4) {
    // serial_trace("start link\n");
    e1000_linkup();
  }

  if (status & 0x10) {
    // good threshold
    // serial_send_string("ethernet Minimum Threshold Reached\n");
  }

  if (status & 0x80) {
    serial_trace("\nethernet receive a message\n");

    uint16_t old_cur;
    bool got_packet = false;

    // serial_trace("rx_cur : %d", rx_cur);
    // serial_trace("  status : %b\n", rx_desc[rx_cur]->status);

    while ((rx_desc[rx_cur]->status & 0x1)) {
      got_packet = true;
      uint8_t *buff = (uint8_t *)rx_desc[rx_cur]->addr;
      uint16_t len = rx_desc[rx_cur]->length;

      // serial_trace("\nlength : %d\n", rx_desc[rx_cur]->length);

      uint8_t *data = (uint8_t *)phys_base_alloc(1 + (len + 16) / 4096);
      memcopy(data, buff, len);

      ethernet_header_t *eth = (ethernet_header_t *)data;
      serial_trace("ETHERNET Received packet of length %d\n", len);
      serial_trace("EHERNE src mac : ");
      for (int i = 0; i < 6; i++) {
        serial_trace("%x ", switch_endian16(eth->src[i]) >> 8);
      }
      serial_trace("\n");

      // handle arp packet
      if (switch_endian16(eth->type) == 0x0806) {
        serial_trace("ARP packet\n");

        arp_header_t *arp = (arp_header_t *)(data + sizeof(ethernet_header_t));
        if (switch_endian16(arp->op) == 0x0001) {
          serial_trace("ARP request\n");

          uint8_t hlen = arp->hlen;
          uint8_t plen = arp->plen;

          uint8_t srchw[hlen];
          serial_trace("src mac : ");
          for (int i = 0; i < hlen; i++) {
            srchw[i] = *(uint8_t *)(data + sizeof(ethernet_header_t) +
                                    sizeof(arp_header_t) + i);
            // srchw[i] = switch_endian16(srchw[i]) >> 8;
            serial_trace("%x ", srchw[i]);
          }
          serial_trace("\n");

          uint8_t srcproto[plen];
          serial_trace("src ip : ");
          for (int i = 0; i < plen; i++) {
            srcproto[i] = *(uint8_t *)(data + sizeof(ethernet_header_t) +
                                       sizeof(arp_header_t) + hlen + i);
            serial_trace("%d ", srcproto[i]);
          }
          serial_trace("\n");

          uint8_t dsthw[hlen];
          serial_trace("dst mac : ");
          for (int i = 0; i < hlen; i++) {
            dsthw[i] = *(uint8_t *)(data + sizeof(ethernet_header_t) +
                                    sizeof(arp_header_t) + hlen + plen + i);
            // dsthw[i] = switch_endian16(dsthw[i]) >> 8;
            serial_trace("%x ", dsthw[i]);
          }
          serial_trace("\n");

          uint8_t dstproto[plen];
          serial_trace("dst ip : ");
          for (int i = 0; i < plen; i++) {
            dstproto[i] =
                *(uint8_t *)(data + sizeof(ethernet_header_t) +
                             sizeof(arp_header_t) + 2 * hlen + plen + i);
            serial_trace("%d ", dstproto[i]);
          }
          serial_trace("\n");

          // send arp reply
          uint8_t *arp_reply_buff = (uint8_t *)phys_base_alloc(
              1 + (sizeof(arp_header_t) + 16) / 4096);
          arp_header_t reply;
          reply.op = switch_endian16(0x002);
          reply.htype = arp->htype;
          reply.ptype = arp->ptype;
          reply.hlen = hlen;
          reply.plen = plen;
          memcopy(arp_reply_buff, &reply, sizeof(arp_header_t));

          uint8_t mac_endian[6];
          for (int i = 0; i < 6; i++) {
            mac_endian[i] = switch_endian16(mac[i]) >> 8;
            serial_trace("%x:", mac_endian[i]);
          }
          serial_trace("\n");

          memcopy(arp_reply_buff + sizeof(arp_header_t), mac_endian,
                  hlen * sizeof(uint8_t));
          memcopy(arp_reply_buff + sizeof(arp_header_t) + hlen, dstproto, plen);

          memcopy(arp_reply_buff + sizeof(arp_header_t) + hlen + plen, srchw,
                  hlen);
          memcopy(arp_reply_buff + sizeof(arp_header_t) + hlen + plen + hlen,
                  srcproto, plen);

          ethernet_header_t *eth_reply = (ethernet_header_t *)phys_base_alloc(
              1 + (sizeof(ethernet_header_t) + 16) / 4096);
          memcopy(eth_reply, eth, sizeof(ethernet_header_t));
          memcopy(eth_reply->src, mac_endian, 6);
          memcopy(eth_reply->dest, eth->src, 6);
          eth_reply->type = switch_endian16(0x0806);

          uint8_t *buf = (uint8_t *)phys_base_alloc(
              1 +
              (sizeof(ethernet_header_t) + sizeof(arp_header_t) + 16) / 4096);
          memcopy(buf, eth_reply, sizeof(ethernet_header_t));
          memcopy((uint8_t *)((uintptr_t)buf + sizeof(ethernet_header_t)),
                  arp_reply_buff,
                  sizeof(arp_header_t) +
                      (hlen + plen + hlen + plen) * sizeof(uint8_t));

          sendPacket(VIRT2PHYS(buf),
                     sizeof(ethernet_header_t) + sizeof(arp_header_t) +
                         (hlen + plen + hlen + plen) * sizeof(uint8_t));
        } else if (arp->op == 0x0200) {
          serial_trace("ARP reply\n");
        }
      }

      // handle ipv4
      else if (switch_endian16(eth->type) == 0x0806) {
      }

      // print buffer in binary
      // for (int i = 0; i < len; i++) {
      //   data_endian[i] = data[len - i - 1];
      //   serial_trace("%x ", data[i]);
      // }

      serial_trace("\n");

      phys_base_free(VIRT2PHYS((uint64_t)data), 1 + (len + 16) / 4096);

      rx_desc[rx_cur]->status = 0;
      old_cur = rx_cur;
      rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;
      writeCommand(REG_RXDESCTAIL, old_cur);
    }
  }

  // TODO: maybe need to handle this
  if (status & (1 << 6)) {
    serial_trace("OVERRUN!!\n");
    uint16_t len = rx_desc[rx_cur]->length;
    writeCommand(0x000d8, (1 << 6));

    // check missed packet cout
    uint32_t mpc = readCommand(0x4010);
    serial_trace("mised packet count : %d\n", mpc);

    // Here you should inject the received packet into your network stack
    serial_trace("ETHERNET Received packet of length %d\n", len);
  }
}

void send_ping(uint8_t *ip) {
  uint8_t *data = (uint8_t *)phys_base_alloc(1 + (sizeof(ethernet_header_t) +
                                                  sizeof(ip_header_t) +
                                                  sizeof(icmp_header_t) + 16) /
                                                     4096);
  ethernet_header_t *eth = (ethernet_header_t *)data;
  eth->type = switch_endian16(0x0800);
  memcopy(eth->dest, mac, 6);
  memcopy(eth->src, mac, 6);

  // ip_header_t *ip_packet = (ip_header_t *)(data + sizeof(ethernet_t));
  // ip_packet->version_ihl = 0x45;
  // ip_packet->total_length = 0;
  // ip_packet->length = switch_endian16(sizeof(ip_header_t) + sizeof(icmp_t));
  // ip_packet->id = 0;
  // ip_packet->flags = 0;
  // ip_packet->ttl = 64;
  // ip_packet->protocol = 1;
  // ip_packet->checksum = 0;
  // memcopy(ip_packet->src, ip, 4);
  // memcopy(ip_packet->dest, ip, 4);
  // ip_packet->checksum =
  //     switch_endian16(checksum((uint16_t *)ip_packet, sizeof(ip_header_t)));

  // icmp_packet_t *icmp_packet = (icmp_packet_t *)(data + sizeof(ethernet_t) +
  // sizeof(ip_header_t)); icmp_packet->type = 8; icmp_packet->code = 0;
  // icmp_packet->checksum = 0;
  // icmp_packet->id = 0;
  // icmp_packet->seq = 0;
  // icmp_packet->checksum =
  //     switch_endian16(checksum((uint16_t *)icmp_packet,
  //     sizeof(icmp_packet_t)));

  sendPacket(VIRT2PHYS(data), sizeof(ethernet_header_t) + sizeof(ip_header_t) +
                                  sizeof(icmp_header_t));
}