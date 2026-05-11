#ifndef __AHCI__AHCI_REG_HPP__
#define __AHCI__AHCI_REG_HPP__

#include <type.h>

typedef volatile struct {
	uint32_t clb;	    // 0x00, command list base address, 1K-byte aligned
	uint32_t clbu;	    // 0x04, command list base address upper 32 bits
	uint32_t fb;	    // 0x08, FIS base address, 256-byte aligned
	uint32_t fbu;	    // 0x0C, FIS base address upper 32 bits
	uint32_t is;	    // 0x10, interrupt status
	uint32_t ie;	    // 0x14, interrupt enable
	uint32_t cmd;	    // 0x18, command and status
	uint32_t rsv0;	    // 0x1C, Reserved
	uint32_t tfd;	    // 0x20, task file data
	uint32_t sig;	    // 0x24, signature
	uint32_t ssts;	    // 0x28, SATA status (SCR0:SStatus)
	uint32_t sctl;	    // 0x2C, SATA control (SCR2:SControl)
	uint32_t serr;	    // 0x30, SATA error (SCR1:SError)
	uint32_t sact;	    // 0x34, SATA active (SCR3:SActive)
	uint32_t ci;	    // 0x38, command issue
	uint32_t sntf;	    // 0x3C, SATA notification (SCR4:SNotification)
	uint32_t fbs;	    // 0x40, FIS-based switch control
	uint32_t rsv1[11];  // 0x44 ~ 0x6F, Reserved
	uint32_t vendor[4]; // 0x70 ~ 0x7F, vendor specific
} ahci_port_t;

typedef volatile struct {
	uint8_t reset : 1;
	uint8_t int_enable : 1;
	uint8_t mrsm : 2;
	uint32_t reserved : 27;
	uint8_t ahci_enable : 1;
} ahci_ghc_t;

typedef volatile struct {
	uint32_t cap;	  // 0x00, Host capability
	uint32_t ghc;	  // 0x04, Global host control
	uint32_t is;	  // 0x08, Interrupt status
	uint32_t pi;	  // 0x0C, Port implemented
	uint32_t vs;	  // 0x10, Version
	uint32_t ccc_ctl; // 0x14, Command completion coalescing control
	uint32_t ccc_pts; // 0x18, Command completion coalescing ports
	uint32_t em_loc;  // 0x1C, Enclosure management location
	uint32_t em_ctl;  // 0x20, Enclosure management control
	uint32_t cap2;	  // 0x24, Host capabilities extended
	uint32_t bohc;	  // 0x28, BIOS/OS handoff control and status

	// 0x2C - 0x9F, Reserved
	uint8_t rsv[0xA0 - 0x2C];

	// 0xA0 - 0xFF, Vendor specific registers
	uint8_t vendor[0x100 - 0xA0];

	// 0x100 - 0x10FF, Port control registers
	ahci_port_t ports[1];
} __attribute__((packed)) ahci_op_t;

// ========== Device Signatures ==========
#define AHCI_SIG_ATA 0x00000101	  // SATA drive
#define AHCI_SIG_ATAPI 0xEB140101 // SATAPI drive
#define AHCI_SIG_SEMB 0xC33C0101  // Enclosure management bridge
#define AHCI_SIG_PM 0x96690101	  // Port multiplier

// ========== SATA Status Register Bits ==========
#define HBA_PxSSTS_DET_MASK 0xF
#define HBA_PxSSTS_DET_NONE 0x0	       // No device detected
#define HBA_PxSSTS_DET_PRESENT 0x1     // Device present, no PHY
#define HBA_PxSSTS_DET_ESTABLISHED 0x3 // Device present and PHY established
#define HBA_PxSSTS_DET_OFFLINE 0x4     // PHY offline

#define HBA_PxSSTS_SPD_MASK (0xF << 4)
#define HBA_PxSSTS_SPD_NONE (0x0 << 4)
#define HBA_PxSSTS_SPD_GEN1 (0x1 << 4) // Gen 1 (1.5 Gbps)
#define HBA_PxSSTS_SPD_GEN2 (0x2 << 4) // Gen 2 (3.0 Gbps)
#define HBA_PxSSTS_SPD_GEN3 (0x3 << 4) // Gen 3 (6.0 Gbps)

#define HBA_PxSSTS_IPM_MASK (0xF << 8)
#define HBA_PxSSTS_IPM_NONE (0x0 << 8)
#define HBA_PxSSTS_IPM_ACTIVE (0x1 << 8)
#define HBA_PxSSTS_IPM_PARTIAL (0x2 << 8)
#define HBA_PxSSTS_IPM_SLUMBER (0x6 << 8)

#define ATA_DEV_BUSY 0x80
#define ATA_DEV_DRQ 0x08

typedef enum {
	FIS_TYPE_REG_H2D = 0x27,   // Register FIS - host to device
	FIS_TYPE_REG_D2H = 0x34,   // Register FIS - device to host
	FIS_TYPE_DMA_ACT = 0x39,   // DMA activate FIS - device to host
	FIS_TYPE_DMA_SETUP = 0x41, // DMA setup FIS - bidirectional
	FIS_TYPE_DATA = 0x46,	   // Data FIS - bidirectional
	FIS_TYPE_BIST = 0x58,	   // BIST activate FIS - bidirectional
	FIS_TYPE_PIO_SETUP = 0x5F, // PIO setup FIS - device to host
	FIS_TYPE_DEV_BITS = 0xA1,  // Set device bits FIS - device to host
} FIS_TYPE;

typedef enum
    : int { AHCI_DEV_NULL = 0, // Tidak ada device
	    AHCI_DEV_SATA,     // SATA drive
	    AHCI_DEV_SATAPI,   // SATAPI drive (CD/DVD)
	    AHCI_DEV_SEMB,     // Enclosure management bridge
	    AHCI_DEV_PM	       // Port multiplier
    } ahci_device_type_t;

typedef struct {
	uint32_t dba;  // Data base address
	uint32_t dbau; // Data base address upper 32 bits
	uint32_t rsv0; // Reserved

	// DW3
	uint32_t dbc : 22; // Byte count, 4M max
	uint32_t rsv1 : 9; // Reserved
	uint32_t i : 1;
} __attribute__((packed)) ahci_prdt_t;

typedef struct {
	// DW0
	uint8_t cfl : 5; // Command FIS length in DWORDS, 2 ~ 16
	uint8_t a : 1;	 // ATAPI
	uint8_t w : 1;	 // Write, 1: H2D, 0: D2H
	uint8_t p : 1;	 // Prefetchable

	uint8_t r : 1;	  // Reset
	uint8_t b : 1;	  // BIST
	uint8_t c : 1;	  // Clear busy upon R_OK
	uint8_t rsv0 : 1; // Reserved
	uint8_t pmp : 4;  // Port multiplier port

	uint16_t prdtl; // Physical region descriptor table length in entries

	// DW1
	volatile uint32_t
		prdbc; // Physical region descriptor byte count transferred

	// DW2, 3
	uint32_t ctba;	// Command table descriptor base address
	uint32_t ctbau; // Command table descriptor base address upper 32 bits

	// DW4 - 7
	uint32_t rsv1[4]; // Reserved
} ahci_cmd_t;

typedef struct {
	uint8_t cfis[64];
	uint8_t acmd[16];
	uint8_t rsv[48];
	ahci_prdt_t prdt[];
} __attribute__((packed)) ahci_cmd_tbl_t;

typedef struct {
	// DWORD 0
	uint8_t fis_type; // FIS_TYPE_REG_H2D

	uint8_t pmport : 4; // Port multiplier
	uint8_t rsv0 : 3;   // Reserved
	uint8_t c : 1;	    // 1: Command, 0: Control

	uint8_t command;  // Command register
	uint8_t featurel; // Feature register, 7:0

	// DWORD 1
	uint8_t lba0;	// LBA low register, 7:0
	uint8_t lba1;	// LBA mid register, 15:8
	uint8_t lba2;	// LBA high register, 23:16
	uint8_t device; // Device register

	// DWORD 2
	uint8_t lba3;	  // LBA register, 31:24
	uint8_t lba4;	  // LBA register, 39:32
	uint8_t lba5;	  // LBA register, 47:40
	uint8_t featureh; // Feature register, 15:8

	// DWORD 3
	uint8_t countl;	 // Count register, 7:0
	uint8_t counth;	 // Count register, 15:8
	uint8_t icc;	 // Isochronous command completion
	uint8_t control; // Control register

	// DWORD 4
	uint8_t rsv1[4]; // Reserved
} ahci_fis_h2d_t;

#endif //__AHCI__AHCI_REG_HPP__