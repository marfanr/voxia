/* #include <firmw/ahci/ahci.h> */
/* #include <firmw/display/vga.h> */
/* #include <firmw/ehci/ehci.h> */
/* #include <firmw/pci/pci.h> */
/* #include <hal/ethernet/e1000/e1000.h> */
/* #include <libk/debug/debug.h> */
/* #include <libk/serial.h> */
/**/
/* typedef struct */
/* { */
/*     const char *id; */
/*     void (*setup) (); */
/* } register_t; */
/**/
/* register_t firmware[] = { */
/*     { .id = "PCI", .setup = pci_setup }, */
/*     // {.id = "Graphic", .setup = vga_setup}, */
/*     // {.id = "AHCI", .setup = install_ahci}, */
/*     // { .id = "USB 2.0", .setup = setup_ehci }, */
/*     // ethernet */
/*     // { .id = "e1000", .setup = eth_e1000_init }, */
/*     // {.id = "Soundblaster", .setup = install_ahci}, */
/*     // {.id = "", .setup = install_ahci}, */
/* }; */
/**/
/* void */
/* install_fimware () */
/* { */
/*     for (int i = 0; i < sizeof (firmware) / sizeof (register_t); i++) */
/*         { */
/*             KDEBUG (1, "Initializing firmware %s", firmware[i].id); */
/*             firmware[i].setup (); */
/*             KDEBUG (1, "[OK] Firmware %s initialized", firmware[i].id); */
/*         } */
/* } */
