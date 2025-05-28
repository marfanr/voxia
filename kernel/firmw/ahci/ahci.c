/* #include <firmw/ahci/ahci.h> */
/* #include <firmw/pci/pci.h> */
/* #include <libk/serial.h> */
/* #include <libk/timer.h> */
/* #include <libk/type.h> */
/* #include <memory/memory_utils.h> */
/* #include <memory/phys_base_allocator.h> */
/**/
/* #define SATA_SIG_ATA 0x00000101   // SATA drive */
/* #define SATA_SIG_ATAPI 0xEB140101 // SATAPI drive */
/* #define SATA_SIG_SEMB 0xC33C0101  // Enclosure management bridge */
/* #define SATA_SIG_PM 0x96690101    // Port multiplier */
/**/
/* pci_device_t ahci_dev; */
/**/
/* void stop_cmd(hba_port_t *port) { */
/*   port->cmd &= ~1; */
/*   port->cmd &= ~(1 << 4); */
/*   while (1) { */
/*     if (port->cmd & (1 << 14)) */
/*       continue; */
/*     if (port->cmd & (1 << 15)) */
/*       continue; */
/*     break; */
/*   } */
/* } */
/**/
/* // Start command engine */
/* void start_cmd(hba_port_t *port) { */
/*   // Wait until CR (bit15) is cleared */
/*   while (port->cmd & (1 << 15)) */
/*     ; */
/**/
/*   // Set FRE (bit4) and ST (bit0) */
/*   port->cmd |= (1 << 4); */
/*   port->cmd |= 1; */
/* } */
/**/
/* #define ATA_DEV_BUSY 0x80 */
/* #define ATA_DEV_DRQ 0x08 */
/**/
/* #define HBA_PxIS_TFES (1 << 30) */
/**/
/* void probe_port(hba_mem_t *hba) { */
/*   uint32_t pi = hba->pi; */
/*   serial_send_string("Port implemented : "); */
/*   serial_send_number(pi, 2); */
/*   serial_send_string("\n"); */
/*   int i = 0; */
/*   while (i < 32) { */
/*     if (pi & 1) { */
/*       uint32_t sts = hba->ports[i].ssts; */
/*       uint8_t ipm = sts & 0xF000; */
/*       uint8_t det = sts & 0xF; */
/**/
/*       if (det == 0 & ipm == 0) { */
/*         pi >>= 1; */
/*         i++; */
/*         continue; */
/*       } */
/**/
/*       serial_send_string("Port "); */
/*       serial_send_number(i, 10); */
/*       serial_send_string(" is an "); */
/**/
/*       uint32_t sig = hba->ports[i].sig; */
/*       switch (sig) { */
/*       case SATA_SIG_ATA: */
/*         serial_send_string("SATA drive\n"); */
/*         break; */
/*       case SATA_SIG_ATAPI: */
/*         serial_send_string("ATAPI drive\n"); */
/*         break; */
/*       case SATA_SIG_SEMB: */
/*         serial_send_string("SEMB drive\n"); */
/*         break; */
/*       case SATA_SIG_PM: */
/*         serial_send_string("PM drive\n"); */
/*         break; */
/*       default: */
/*         serial_send_string("Unknown drive\n"); */
/*         break; */
/*       } */
/**/
/*       stop_cmd(&hba->ports[i]); */
/**/
/*       hba->ports[i].clb = (uint32_t)phys_base_alloc(1); */
/*       memset((void *)hba->ports[i].clb, 0, 1024); */
/*       hba->ports[i].clbu = 0; */
/**/
/*       hba->ports[i].fb = (uint32_t)phys_base_alloc(1); */
/*       memset((void *)hba->ports[i].fb, 0, 256); */
/*       hba->ports[i].fbu = 0; */
/**/
/*       // set cmdhader */
/*       uint32_t cmdbase = (uint32_t)phys_base_alloc(5); */
/*       HBA_CMD_HEADER *cmd = (HBA_CMD_HEADER *)hba->ports[i].clb; */
/*       for (int i = 0; i < 32; i++) { */
/*         cmd[i].prdtl = 8; // 8 prdt entries per command table */
/*                           // 256 bytes per command table, 64+16+48+16*8 */
/*         // Command table offset: 40K  + cmdheader_index*256 */
/*         cmd[i].ctba = cmdbase + (40 << 10) + (i << 8); */
/*         cmd[i].ctbau = 0; */
/*         memset((void *)cmd[i].ctba, 0, 256); */
/*       } */
/**/
/*       start_cmd(&hba->ports[i]); */
/*     } */
/*     pi >>= 1; */
/*     i++; */
/*   } */
/* } */
/**/
/* // Find a free command list slot */
/* int find_cmdslot(hba_port_t *port) { */
/*   // If not set in SACT and CI, the slot is free */
/*   uint32_t slots = (port->sact | port->ci); */
/*   for (int i = 0; i < 32; i++) { */
/*     if ((slots & 1) == 0) */
/*       return i; */
/*     slots >>= 1; */
/*   } */
/*   return -1; */
/* } */
/**/
/* boolean_t read(hba_port_t *port, uint32_t startl, uint32_t starth, uint32_t
 * count, */
/*           uint16_t *buf) { */
/*   port->is = (uint32_t)-1; // becomes 0xFFFFFFFF when casted to uint32_t */
/*   int slot = find_cmdslot(port); */
/*   int spin = 0; */
/*   if (slot == -1) */
/*     return 0; */
/**/
/*   serial_send_string("Slot : "); */
/*   serial_send_number(slot, 10); */
/*   serial_send_string("\n"); */
/**/
/*   HBA_CMD_HEADER *cmdheader = (HBA_CMD_HEADER *)port->clb; */
/*   cmdheader += slot; */
/*   cmdheader->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t); // Command FIS
 * size */
/*   cmdheader->w = 0;                                        // Read from
 * device */
/*   cmdheader->prdtl = (uint16_t)((count - 1) >> 4) + 1;     // PRDT entries
 * count */
/**/
/*   HBA_CMD_TBL *cmdtbl = (HBA_CMD_TBL *)(cmdheader->ctba); */
/*   memset(cmdtbl, 0, */
/*          sizeof(HBA_CMD_TBL) + (cmdheader->prdtl - 1) *
 * sizeof(HBA_PRDT_ENTRY)); */
/**/
/*   // 8K bytes (16 sectors) per PRDT */
/*   int i = 0; */
/*   for (i = 0; i < cmdheader->prdtl - 1; i++) { */
/*     cmdtbl->prdt_entry[i].dba = (uint32_t)buf; */
/*     cmdtbl->prdt_entry[i].dbc = */
/*         8 * 1024 - 1; // 8K bytes (this value should always be set to 1 less
 */
/*                       // than the actual value) */
/*     cmdtbl->prdt_entry[i].i = 1; */
/*     buf += 4 * 1024; // 4K words */
/*     count -= 16;     // 16 sectors */
/*   } */
/*   // Last entry */
/*   cmdtbl->prdt_entry[i].dba = (uint32_t)buf; */
/*   cmdtbl->prdt_entry[i].dbc = (count << 9) - 1; // 512 bytes per sector */
/*   cmdtbl->prdt_entry[i].i = 1; */
/**/
/*   // Setup command */
/*   FIS_REG_H2D *cmdfis = (FIS_REG_H2D *)(&cmdtbl->cfis); */
/**/
/*   cmdfis->fis_type = FIS_TYPE_REG_H2D; */
/*   cmdfis->c = 1;          // Command */
/*   cmdfis->command = 0x25; // ATA_CMD_READ_DMA_EX; */
/**/
/*   cmdfis->lba0 = (uint8_t)startl; */
/*   cmdfis->lba1 = (uint8_t)(startl >> 8); */
/*   cmdfis->lba2 = (uint8_t)(startl >> 16); */
/*   cmdfis->device = 1 << 6; // LBA mode */
/**/
/*   cmdfis->lba3 = (uint8_t)(startl >> 24); */
/*   cmdfis->lba4 = (uint8_t)starth; */
/*   cmdfis->lba5 = (uint8_t)(starth >> 8); */
/**/
/*   cmdfis->countl = count & 0xFF; */
/*   cmdfis->counth = (count >> 8) & 0xFF; */
/**/
/*   // The below loop waits until the port is no longer busy before issuing a
 * new */
/*   // command */
/*   while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000) { */
/*     spin++; */
/*   } */
/*   if (spin == 1000000) { */
/*     serial_send_string("Port is hung\n"); */
/*     return 0; */
/*   } */
/**/
/*   port->ci = 1 << slot; // Issue command */
/**/
/*   // Wait for completion */
/*   while (1) { */
/*     // In some longer duration reads, it may be helpful to spin on the DPS
 * bit */
/*     // in the PxIS port field as well (1 << 5) */
/*     if ((port->ci & (1 << slot)) == 0) */
/*       break; */
/*     if (port->is & HBA_PxIS_TFES) // Task file error */
/*     { */
/*       serial_send_string("Read disk error\n"); */
/*       return 0; */
/*     } */
/*   } */
/**/
/*   // Check again */
/*   if (port->is & HBA_PxIS_TFES) { */
/*     serial_send_string("Read disk error\n"); */
/*     return 0; */
/*   } */
/**/
/*   return 1; */
/* } */
/**/
/* void install_ahci() { */
/*   for (int i = 0; i < 32; i++) { */
/*     // TODO: find with propper way */
/*     uint8_t subclass = pci_devices[i].subclass; */
/*     uint8_t class = pci_devices[i].class; */
/*     if (subclass == 0x6 && */
/*         class == 0x1) { // EHCI (Not properly implemented but ok for now) */
/*       ahci_dev = pci_devices[i]; */
/*       serial_send_string("\nFound AHCI/ATA device\n"); */
/*       break; */
/*     } */
/*   } */
/**/
/*   uint8_t *ahcibar = (uint8_t *)(ahci_dev.bar[5]); */
/*   boolean_t type = (ahci_dev.bar[5] & 0x1) == 0; */
/*   serial_send_string("AHCI Type : "); */
/*   serial_send_number(type, 10); */
/*   serial_send_string("\n"); */
/**/
/*   hba_mem_t *hba_mem = (hba_mem_t *)ahcibar; */
/*   boolean_t is_64_bit = (hba_mem->cap & 0x80000000); */
/*   serial_send_string("AHCI 64 bit : "); */
/*   serial_send_number(is_64_bit, 2); */
/*   serial_send_string("\n"); */
/**/
/*   hba_mem->ghc &= (1 << 0);             // Reset AHCI */
/*   hba_mem->ghc &= (1 << 31) | (1 << 1); // Enable AHCI */
/*   serial_send_string("AHCI has reseted and reenabled\n"); */
/**/
/*   probe_port(hba_mem); */
/**/
/*   uint16_t *buf = (uint16_t *)phys_base_alloc(1); */
/*   memset(buf, 0, 512); */
/*   serial_send_string("Buffer : "); */
/*   serial_send_number((uint32_t)*buf, 16); */
/*   serial_send_string("\n"); */
/**/
/*   boolean_t s = read(&hba_mem->ports[0], 0, 0, 512, buf); */
/*   serial_send_string("Success read 512 byte from 0x0 on HDD : "); */
/*   serial_send_number(s, 2); */
/*   serial_send_string("\n"); */
/**/
/*   serial_send_string("Data : "); */
/*   for (int i = 0; i < 512; i++) { */
/*     serial_send_number(buf[i], 16); */
/*     serial_send_string(" "); */
/*   } */
/*   serial_send_string("\n"); */
/* } */
