// #include "./intel_hda.h"

// #include <firmw/pci/pci.h>
// #include <libk/debug/debug.h>
// #include <libk/serial.h>
// #include <str.h>
// #include <libk/timer.h>
// #include <type.h>
// #include <memory/memory_utils.h>
// #include <memory/phys_base_allocator.h>
// #include <vfs/vfs.h>

// /* Variabel global HDA */
// pci_device_t hda_device;

// static volatile uint32_t *hda_corr         = 0; // CORB: Command Output Ring Buffer
// static volatile uint32_t *hda_rirrb        = 0; // RIRB: Response Input Ring Buffer
// static uint8_t            hda_corb_pointer = 1;
// static uint8_t            hda_rirb_pointer = 1;
// static int                ticks            = 0;
// static uint8_t            pin_speaker_node = 0, pin_alt_speaker_node = 0;

// /* Fungsi-fungsi MMIO dasar */
// uint8_t
// mmio_readb(uint16_t offset)
// {
//     return *((volatile uint8_t *)(hda_device.bar[0] + offset));
// }

// void
// mmio_writeb(uint16_t offset, uint8_t data)
// {
//     *((volatile uint8_t *)(hda_device.bar[0] + offset)) = data;
// }

// uint16_t
// mmio_readw(uint16_t offset)
// {
//     return *((volatile uint16_t *)(hda_device.bar[0] + offset));
// }

// void
// mmio_writew(uint16_t offset, uint16_t data)
// {
//     *((volatile uint16_t *)(hda_device.bar[0] + offset)) = data;
// }

// uint32_t
// mmio_readl(uint16_t offset)
// {
//     return *((volatile uint32_t *)(hda_device.bar[0] + offset));
// }

// void
// mmio_writel(uint16_t offset, uint32_t data)
// {
//     *((volatile uint32_t *)(hda_device.bar[0] + offset)) = data;
// }

// /* Prototipe fungsi */
// void parse_codec(int codec);
// void audio_function_group_process(int codec, int node);

// /*
//  * Kirim perintah (verb) melalui PIO.
//  * Metode ini mengirim perintah secara langsung dan menunggu respons.
//  */
// uint32_t
// send_verb_pio(uint32_t codec, uint32_t node, uint32_t verb, uint32_t cmd)
// {
//     uint32_t data = ((codec << 28) | (node << 20) | (verb << 8) | cmd);
//     // Mulai PIO: set bit (misal: offset 0x68) sebelum menulis data
//     mmio_writew(0x68, 0x2);
//     mmio_writel(0x60, data);
//     mmio_writew(0x68, 0x1);

//     ticks = 0;
//     while (ticks < 4)
//     {
//         ticks++;
//         usleep(100);
//         // Cek status PIO (offset 0x68)
//         if ((mmio_readw(0x68) & 0x3) == 0x2)
//         {
//             mmio_writew(0x68, 0x2);
//             return mmio_readl(0x64);
//         }
//     }
//     return 0;
// }

// /*
//  * Kirim perintah melalui mekanisme CORB/RIRB.
//  * Perhatikan bahwa implementasi ini harus disesuaikan dengan
//  * spesifikasi dan pengaturan sinkronisasi hardware.
//  */
// uint32_t
// send_verb(uint32_t codec, uint32_t node, uint32_t command, uint32_t verb)
// {
//     serial_trace("HDA CORB base: 0x%x\n", (uintptr_t)hda_corr);
//     uint32_t data = ((codec << 28) | (node << 20) | (command << 8) | verb);
//     serial_trace("Sending verb: 0x%x\n", data);
//     hda_corr[hda_corb_pointer] = data;
//     serial_trace("CORB pointer: %d -> 0x%x\n", hda_corb_pointer, hda_corr[hda_corb_pointer]);

//     mmio_writew(0x48, hda_corb_pointer);
//     int c = 0;
//     while (c < 3)
//     {
//         usleep(500);
//         uint8_t a = mmio_readb(0x58);
//         if (a == hda_corb_pointer)
//         {
//             break;
//         }
//         c++;
//     }

//     data             = hda_rirrb[hda_rirb_pointer * 2];
//     hda_rirb_pointer = (hda_rirb_pointer + 1) % 256;
//     hda_corb_pointer = (hda_corb_pointer + 1) % 256;

//     serial_trace("Verb response: 0x%x\n", data);
//     return data;
// }

// /*
//  * Inisialisasi Intel HDA:
//  *  - Mencari perangkat PCI dengan class 0x04 (audio) dan subclass 0x03 (HDA)
//  *  - Reset dan konfigurasi awal register
//  *  - Alokasi dan inisialisasi buffer CORB dan RIRB
//  */
// void
// intel_hda_init(void)
// {
//     /* Cari perangkat HDA pada daftar PCI */
//     for (int i = 0; i < 32; i++)
//     {
//         if (pci_devices[i].class == 0x4 && pci_devices[i].subclass == 0x3)
//         {
//             serial_trace("\nFound Intel HDA device BAR at: 0x%x\n", pci_devices[i].bar[0]);
//             hda_device = pci_devices[i];
//             enable_bus_mastering(hda_device);
//             break;
//         }
//     }

//     /* Reset HDA controller */
//     mmio_writel(INTEL_HDA_GCTL_OFFSET, 0);
//     usleep(100);
//     if ((mmio_readb(INTEL_HDA_GCTL_OFFSET) & 0x1) == 0)
//         serial_trace("Intel HDA reset success\n");

//     mmio_writel(INTEL_HDA_GCTL_OFFSET, 1);
//     usleep(100);
//     if ((mmio_readb(INTEL_HDA_GCTL_OFFSET) & 0x1) == 1)
//         serial_trace("Intel HDA exited reset state successfully\n");

//     /* Baca versi spesifikasi */
//     uint8_t major_version = mmio_readb(INTEL_HDA_VMAJ_OFFSET);
//     uint8_t minor_version = mmio_readb(INTEL_HDA_VMIN_OFFSET);
//     serial_trace("Intel HDA version: %d.%d\n", major_version, minor_version);

//     /* Nonaktifkan interrupt */
//     mmio_writel(INTEL_HDA_INT_OFFSET, 0);

//     /* Konfigurasi DMA Position Buffer */
//     mmio_writel(0x70, 0);
//     mmio_writel(0x74, 0);

//     /* Nonaktifkan stream synchronisation dan CORB/RIRB */
//     mmio_writel(0x38, 0);
//     mmio_writeb(0x4C, 0);
//     mmio_writeb(0x5C, 0);

//     /* Alokasikan dan inisialisasi CORB */
//     uint8_t corb_size_reg = mmio_readb(0x4E);
//     serial_trace("CORB size register: 0x%x\n", corb_size_reg);

//     /* Alokasikan setidaknya satu halaman dan pastikan alignment 128-byte */
//     hda_corr = (volatile uint32_t *)VIRT2PHYS(phys_base_alloc(1));
//     hda_corr = (volatile uint32_t *)((((uint32_t)hda_corr + 0x80 - 1) / 0x80) * 0x80);
//     serial_trace("CORB allocated at: 0x%x\n", (uint32_t)hda_corr);
//     memset((void *)hda_corr, 0xFF, 256 * 8);
//     mmio_writel(0x40, (uint32_t)hda_corr);
//     mmio_writel(0x44, 0);
//     /* Set CORB ring size ke 256 entri (0b10) */
//     mmio_writeb(0x4E, 0x2);

//     /* Reset pointer CORB */
//     mmio_writew(0x4A, 0x8000);
//     while ((mmio_readw(0x4A) & 0x8000) != 0x8000)
//         usleep(100);
//     mmio_writew(0x4A, 0);
//     while ((mmio_readw(0x4A) & 0x8000) != 0)
//         usleep(100);
//     if ((mmio_readw(0x4A) & 0x8000) == 0x8000)
//         serial_trace("CORB reset failed\n");

//     mmio_writel(0x48, 0);

//     /* Alokasikan dan inisialisasi RIRB */
//     uint8_t rirb_size_reg = mmio_readb(0x5E);
//     serial_trace("RIRB size register: 0x%x\n", rirb_size_reg);
//     hda_rirrb = (volatile uint32_t *)VIRT2PHYS(phys_base_alloc(1));
//     memset((void *)hda_rirrb, 0, 256 * 8);
//     mmio_writel(0x50, (uint32_t)hda_rirrb);
//     mmio_writel(0x54, 0);
//     /* Set RIRB ring size ke 256 entri */
//     mmio_writeb(0x5E, 0x2);

//     /* Reset pointer RIRB */
//     mmio_writew(0x58, 0x8000);
//     while ((mmio_readw(0x58) & 0x8000) != 0)
//         ;
//     usleep(500);
//     mmio_writew(0x5A, 0xFF); // Set response interrupt count

//     /* Konfigurasi DMA Position Buffer */
//     mmio_writel(0x70, 1);
//     mmio_writel(0x74, 0x0);

//     /* Nyalakan CORB dan RIRB (DMA mode), namun jika ada masalah pada QEMU,
//      * gunakan PIO */
//     mmio_writeb(0x4C, 0x2);
//     mmio_writeb(0x5C, 0x2);
//     // Jika DMA bermasalah, nonaktifkan DMA:
//     mmio_writeb(0x4C, 0);
//     mmio_writeb(0x5C, 0);

//     /* Pindai CODEC menggunakan mode PIO */
//     for (int i = 0; i < 16; i++)
//     {
//         uint32_t codecid = send_verb_pio(i, 0, 0xF00, 0);
//         if (codecid != 0)
//         {
//             serial_trace("\nFound CODEC (PIO) id: 0x%x\n", codecid);
//             parse_codec(i);
//         }
//     }
//     serial_trace("Intel HDA init done\n\n");
// }

// /*
//  * Fungsi parse_codec mengambil informasi CODEC, seperti vendor/device,
//  * revision, dan data subordinate untuk menentukan function group.
//  */
// void
// parse_codec(int codec)
// {
//     uint32_t info      = send_verb_pio(codec, 0, 0xF00, 0);
//     uint16_t device_id = info & 0xFFFF;
//     uint16_t vendor_id = (info >> 16) & 0xFFFF;

//     info                 = send_verb_pio(codec, 0, 0xF00, 0x2);
//     uint16_t revision_id = info >> 16;
//     serial_trace("Codec %d: Vendor 0x%x, Device 0x%x, Revision 0x%x\n", codec, vendor_id,
//     device_id,
//                  revision_id);

//     uint32_t subordinate_info = send_verb_pio(codec, 0, 0xF00, 0x4);
//     serial_trace("Subordinate info: 0x%x\n", subordinate_info);
//     uint8_t total_nodes = subordinate_info & 0xFF;
//     uint8_t start_node  = (subordinate_info >> 16) & 0xFF;
//     serial_trace("Total nodes: %d, Start node: %d\n", total_nodes, start_node);

//     for (uint8_t i = start_node; i < start_node + total_nodes; i++)
//     {
//         uint32_t node_info = send_verb_pio(codec, i, 0xF00, 0x5);
//         if ((node_info & 0x7F) == 0x1)
//         {
//             serial_trace("Node 0x%x is an Audio Function Group\n", i);
//             audio_function_group_process(codec, i);
//             return;
//         }
//     }
// }

// /*
//  * Fungsi get_connection_list mengambil entri daftar koneksi dari suatu node.
//  * Memilih format pendek atau panjang sesuai bit paling atas dari response.
//  */
// uint16_t
// get_connection_list(int codec, int node, uint32_t connection_number)
// {
//     uint32_t connection_lst_cap = send_verb_pio(codec, node, 0xF00, 0x0E);
//     if (connection_number >= (connection_lst_cap & 0x7F))
//         return 0;

//     if ((connection_lst_cap & 0x80) == 0x00)
//     { // short form
//         return (send_verb_pio(codec, node, 0xF02, ((connection_number / 4) * 4)) >>
//                 ((connection_number % 4) * 8)) &
//                0xFF;
//     }
//     else
//     { // long form
//         return (send_verb_pio(codec, node, 0xF02, ((connection_number / 2) * 2)) >>
//                 ((connection_number % 2) * 16)) &
//                0xFFFF;
//     }
// }

// /*
//  * Fungsi set_gain_node mengatur gain (volume) untuk sebuah node.
//  */
// void
// set_gain_node(int codec, int node, int type, uint16_t cap, uint16_t gain)
// {
//     uint16_t payload = 0x3000;
//     if (type == 0x1)
//         payload |= 0x8000;
//     if (gain == 0 && (cap & 0x80000000))
//         payload |= 0x80; // mute
//     else
//         payload |= (((cap >> 8) & 0x7F) * gain / 100); // konversi 0-100 ke range node
//     send_verb_pio(codec, node, 0x3, payload);
// }

// /*
//  * Fungsi audio_function_group_process melakukan inisialisasi untuk Audio
//  * Function Group:
//  *  - Reset, power on, aktifkan unsolicited response,
//  *  - Baca capability widget, cari widget output,
//  *  - Jika ditemukan, konfigurasi node output speaker.
//  */
// void
// audio_function_group_process(int codec, int node)
// {
//     uint32_t node_info = send_verb_pio(codec, node, 0xF00, 0x5);
//     uint8_t  node_type = node_info & 0xFF;
//     uint8_t  unsol     = (node_info >> 8) & 0xFF;
//     serial_trace("Node 0x%x type: %d, unsol: %d\n", node, node_type, unsol);

//     /* Inisialisasi AFG */
//     send_verb_pio(codec, node, 0x7FF, 0); // Reset function group
//     send_verb_pio(codec, node, 0x705, 0); // Power on
//     send_verb_pio(codec, node, 0x708, 0); // Enable unsolicited response

//     uint32_t sample_capability     = send_verb_pio(codec, node, 0xF00, 0x0A);
//     uint32_t supported_format      = send_verb_pio(codec, node, 0xF00, 0x0B);
//     uint32_t pin_capability        = send_verb_pio(codec, node, 0xF00, 0x0C);
//     uint32_t input_amp_capability  = send_verb_pio(codec, node, 0xF00, 0x0D);
//     uint32_t output_amp_capability = send_verb_pio(codec, node, 0xF00, 0x0E);

//     serial_trace("Sample capability: 0x%x\n", sample_capability);
//     serial_trace("Supported format: AC3: %d, Float32: %d, PCM: %d\n",
//                  (supported_format & 0b100) != 0, (supported_format & 0b010) != 0,
//                  (supported_format & 0b001) != 0);
//     serial_trace("Pin capability: 0x%x\n", pin_capability);

//     uint32_t subordinate_info = send_verb_pio(codec, node, 0xF00, 0x4);
//     uint8_t  total_nodes      = subordinate_info & 0xFF;
//     uint8_t  start_node       = (subordinate_info >> 16) & 0xFF;
//     serial_trace("Total nodes: %d, Start node: %d\n", total_nodes, start_node);

//     for (uint8_t i = start_node; i < start_node + total_nodes; i++)
//     {
//         uint32_t widget_cap  = send_verb_pio(codec, i, 0xF00, 0x9);
//         uint8_t  widget_type = (widget_cap >> 20) & 0xF;
//         if (widget_type == 0)
//         {
//             serial_trace("Widget 0x%x is an audio output\n", i);
//             send_verb_pio(codec, i, 0x706, 0x0);
//             // pin_speaker_node = i;
//         }
//         else if (widget_type == 1)
//             serial_trace("Widget 0x%x is an audio input\n", i);
//         else if (widget_type == 0x4)
//         {
//             serial_trace("Widget 0x%x is a complex widget\n", i);
//             widget_type = (send_verb_pio(codec, i, 0xF1C, 0x00) >> 20) & 0xF;
//             if (widget_type == 0x0)
//             {
//                 serial_trace("Widget 0x%x is a Line Out widget\n", i);
//                 pin_alt_speaker_node = i;
//             }
//             else if (widget_type == 0x1)
//                 serial_trace("Widget 0x%x is a Speaker widget\n", i);
//             else if (widget_type == 0x2)
//                 serial_trace("Widget 0x%x is a HP Out widget\n", i);
//             else if (widget_type == 0x3)
//                 serial_trace("Widget 0x%x is a CD widget\n", i);
//             else if (widget_type == 0x4)
//                 serial_trace("Widget 0x%x is a SPDIF Out widget\n", i);
//             else if (widget_type == 0x5)
//                 serial_trace("Widget 0x%x is a Digital Other Out widget\n", i);
//             else if (widget_type == 0x6)
//                 serial_trace("Widget 0x%x is a Modem Line Side widget\n", i);
//             else if (widget_type == 0x7)
//                 serial_trace("Widget 0x%x is a Modem Handset Side widget\n", i);
//             else if (widget_type == 0x8)
//                 serial_trace("Widget 0x%x is a Line In widget\n", i);
//             else if (widget_type == 0x9)
//                 serial_trace("Widget 0x%x is a Aux widget\n", i);
//             else if (widget_type == 0xA)
//                 serial_trace("Widget 0x%x is a Mic In widget\n", i);
//             else if (widget_type == 0xB)
//                 serial_trace("Widget 0x%x is a Telephony widget\n", i);
//             else if (widget_type == 0xC)
//                 serial_trace("Widget 0x%x is a SPDIF In widget\n", i);
//             else if (widget_type == 0xD)
//                 serial_trace("Widget 0x%x is a Digital Other In widget\n", i);
//             else if (widget_type == 0xE)
//                 serial_trace("Widget 0x%x is a Analog Other widget\n", i);
//             else if (widget_type == 0xF)
//                 serial_trace("Widget 0x%x is a Reserved widget\n", i);
//         }
//         else if (widget_type == 0x6)
//             serial_trace("Widget 0x%x is a volume knob\n", i);
//         else if (widget_type == 0x7)
//             serial_trace("Widget 0x%x is a beep generator\n", i);
//         else if (widget_type == 0x8)
//             serial_trace("Widget 0x%x is a vendor defined\n", i);
//         else
//             serial_trace("Widget 0x%x is a reserved\n", i);

//         /* Coba cari output node yang terhubung */
//         uint8_t out_amp_present = widget_cap & 0x4;
//         if (out_amp_present)
//         {
//             serial_trace("Output amp present on widget 0x%x\n", i);
//             if ((send_verb_pio(codec, i, 0xF1C, 0) >> 30) != 0x1)
//             {
//                 if ((send_verb_pio(codec, i, 0xF00, 0x0C) & 0x10) == 0x10)
//                 {
//                     serial_trace("Codec %d, Node 0x%x connected to "
//                                  "output device\n",
//                                  codec, i);
//                     pin_speaker_node = i;
//                 }
//                 else
//                 {
//                     serial_trace("no output device\n");
//                 }
//             }
//         }

//         /* Tampilkan daftar koneksi (jika ada) */
//         uint8_t  con_entry_number = 0;
//         uint16_t con_list         = get_connection_list(codec, i, con_entry_number);
//         while (con_list != 0)
//         {
//             serial_trace("Node 0x%x Connection list entry: %d\n", i, con_list);
//             con_entry_number++;
//             con_list = get_connection_list(codec, i, con_entry_number);
//         }
//     }

//     /* Jika ditemukan output node untuk speaker, lakukan inisialisasi lebih
//      * lanjut */
//     if (pin_speaker_node != 0)
//     {
//         serial_trace("Found speaker node: 0x%x\n", pin_speaker_node);
//         send_verb_pio(codec, pin_speaker_node, 0x705,
//                       0); // Power on speaker node
//         send_verb_pio(codec, pin_speaker_node, 0x708,
//                       0); // Enable unsolicited response
//         send_verb_pio(codec, pin_speaker_node, 0x703,
//                       0); // Stop processing (jika diperlukan)
//         send_verb_pio(codec, pin_speaker_node, 0x706,
//                       0x10); // Set stream/channel
//         uint16_t pin_out_cap = send_verb_pio(codec, pin_speaker_node, 0xF00, 0x012);
//         set_gain_node(codec, pin_speaker_node, 1, pin_out_cap, 100);
//         serial_trace("Audio output node initialized\n");
//     }
//     else if (pin_alt_speaker_node != 0)
//     {
//         serial_trace("Found alternate speaker node: 0x%x\n", pin_alt_speaker_node);
//         send_verb_pio(codec, pin_alt_speaker_node, 0x705,
//                       0); // Power on speaker node
//         send_verb_pio(codec, pin_alt_speaker_node, 0x708,
//                       0); // Enable unsolicited response
//         send_verb_pio(codec, pin_alt_speaker_node, 0x703,
//                       0); // Stop processing (jika diperlukan)
//         send_verb_pio(codec, pin_alt_speaker_node, 0x706,
//                       0x10); // Set stream/channel
//         uint16_t pin_out_cap = send_verb_pio(codec, pin_alt_speaker_node, 0xF00, 0x012);
//         set_gain_node(codec, pin_alt_speaker_node, 1, pin_out_cap, 100);
//         serial_trace("Alternate audio output node initialized\n");
//         pin_speaker_node = pin_alt_speaker_node;
//     }
//     else
//     {
//         serial_trace("No speaker node found\n");
//     }
// }

// /*
//  * Fungsi intel_hda_play:
//  *  - Membaca file audio dari sistem file
//  *  - Mengatur stream descriptor untuk output stream
//  *  - Memulai pemutaran
//  */
// struct stream_buffer
// {
//     uint64_t buffer;
//     uint32_t length;
//     uint32_t ioc;
// } __attribute__((packed));

// uint16_t
// hda_return_sound_data_format(uint32_t sample_rate, uint32_t channels, uint32_t bits_per_sample)
// {
//     uint16_t data_format = 0;

//     // channels
//     data_format = (channels - 1);

//     // bits per sample
//     if (bits_per_sample == 16)
//     {
//         data_format |= ((0b001) << 4);
//     }
//     else if (bits_per_sample == 20)
//     {
//         data_format |= ((0b010) << 4);
//     }
//     else if (bits_per_sample == 24)
//     {
//         data_format |= ((0b011) << 4);
//     }
//     else if (bits_per_sample == 32)
//     {
//         data_format |= ((0b100) << 4);
//     }

//     // sample rate
//     if (sample_rate == 48000)
//     {
//         data_format |= ((0b0000000) << 8);
//     }
//     else if (sample_rate == 44100)
//     {
//         data_format |= ((0b1000000) << 8);
//     }
//     else if (sample_rate == 32000)
//     {
//         data_format |= ((0b0001010) << 8);
//     }
//     else if (sample_rate == 22050)
//     {
//         data_format |= ((0b1000001) << 8);
//     }
//     else if (sample_rate == 16000)
//     {
//         data_format |= ((0b0000010) << 8);
//     }
//     else if (sample_rate == 11025)
//     {
//         data_format |= ((0b1000011) << 8);
//     }
//     else if (sample_rate == 8000)
//     {
//         data_format |= ((0b0000101) << 8);
//     }
//     else if (sample_rate == 88200)
//     {
//         data_format |= ((0b1001000) << 8);
//     }
//     else if (sample_rate == 96000)
//     {
//         data_format |= ((0b0001000) << 8);
//     }
//     else if (sample_rate == 176400)
//     {
//         data_format |= ((0b1011000) << 8);
//     }
//     else if (sample_rate == 192000)
//     {
//         data_format |= ((0b0011000) << 8);
//     }

//     return data_format;
// }

// void
// test_beep(void)
// {
//     int beep_node = -1;
//     // Asumsikan codec 0; iterasi node 0 sampai 15 (sesuaikan dengan rentang
//     // node yang valid)
//     for (int node = 0; node < 16; node++)
//     {
//         uint32_t widget_cap  = send_verb_pio(0, node, 0xF00, 0x9);
//         uint8_t  widget_type = (widget_cap >> 20) & 0xF;
//         if (widget_type == 0x7)
//         {
//             beep_node = node;
//             serial_trace("Found beep generator at node 0x%x\n", node);
//             break;
//         }
//     }
//     if (beep_node == -1)
//     {
//         serial_trace("No beep generator found\n");
//         return;
//     }

//     /* Memicu beep:
//        Contoh: mengirimkan verb 0x70 dengan payload 0x1 untuk memulai beep,
//        kemudian 0x70 dengan payload 0x0 untuk menghentikannya.
//        (Nilai-nilai ini hanya contoh; sesuaikan dengan dokumentasi codec Anda)
//      */
//     serial_trace("Triggering beep...\n");
//     send_verb_pio(0, beep_node, 0x70, 0x1);
//     usleep(500000); // Beep selama 0.5 detik
//     send_verb_pio(0, beep_node, 0x70, 0x0);
//     serial_trace("Beep finished.\n");
// }

// void
// intel_hda_play(const char *file_path)
// {
//     int                   fd = vfs_open(file_path, 0);
//     struct vfs_file_stats stats;
//     int                   fs_resp = vfs_fstat(fd, &stats);
//     serial_trace("File size: %d bytes\n", stats.size);

//     /* Alokasikan buffer untuk file audio dan pastikan alamat fisiknya benar */
//     uint8_t *file = (uint8_t *)VIRT2PHYS(phys_base_alloc((stats.size / 4096) + 1));
//     file          = (uint8_t *)(((uint32_t)file + 0x7F) & ~0x7F);
//     serial_trace("File buffer: 0x%x\n", (uint32_t)file);
//     int o = vfs_read(fd, file, stats.size);
//     if (o != 0)
//     {
//         serial_trace("Error reading file\n");
//         return;
//     }

//     uint32_t pos = ((mmio_readw(0x00) >> 8) & 0xF);
//     serial_trace("HDA stream input stream  number %d\n", pos);

//     uint32_t output_pos = ((mmio_readw(0x00) >> 12) & 0xF);
//     serial_trace("HDA stream output stream  number %d\n", output_pos);

//     /* Atur alamat stream output dari register stream descriptor.
//        Perhitungan offset disesuaikan dengan jumlah stream input/output yang
//        tersedia. */
//     volatile uint32_t *hda_stream_output_base =
//         (volatile uint32_t *)(hda_device.bar[0] + 0x80 + (0x20 * ((mmio_readw(0x00) >> 8) &
//         0xF)));
//     serial_trace("HDA stream output base: 0x%x\n", (uint32_t)hda_stream_output_base);

//     /* Reset stream: update control register untuk memulai stream */
//     uint32_t stream_desc = *((volatile uint32_t *)hda_stream_output_base);
//     stream_desc &= ~0x2; // Clear flag tertentu (sesuaikan dengan spesifikasi)
//     stream_desc |= 0x1;  // Set stream run flag
//     *((volatile uint32_t *)hda_stream_output_base) = stream_desc;

//     int counter                                      = 0;
//     *((volatile uint32_t *)(hda_stream_output_base)) = 0;

//     while ((*((volatile uint32_t *)(hda_stream_output_base + 0)) & 0x1) != 0)
//     {
//         usleep(100);
//         counter++;
//         if (counter > 100)
//         {
//             serial_trace("\nHDA: can not reset stream");
//             break;
//         }
//     }

//     *((volatile uint32_t *)(hda_stream_output_base + 0x03)) = 0x1C;

//     /* Buat stream buffer descriptor */
//     struct stream_buffer *stream_list = (struct stream_buffer *)((uint64_t)VIRT2PHYS(
//         phys_base_alloc(1 + (sizeof(struct stream_buffer) * 2))));
//     stream_list = (struct stream_buffer *)(((uint32_t)stream_list + 0x7F) & ~0x7F);
//     memset(stream_list, 0, sizeof(struct stream_buffer) * 2);
//     asm("wbinvd");

//     for (int i = 0; i < 1; i++)
//     {
//         stream_list[i].buffer = (uint64_t)(file);
//         stream_list[i].length = stats.size;
//         stream_list[i].ioc    = 1;
//     }

//     serial_trace("Stream buffer descriptor: 0x%x\n", (uint32_t)stream_list);
//     // asm("wbinvd"); // Flush cache (jika diperlukan)

//     asm("wbinvd");
//     mmio_writel(0x80 + pos * 0x20 + 0x18, (uint32_t)(uintptr_t)stream_list);
//     mmio_writel(0x80 + pos * 0x20 + 0x1C, (uint32_t)((uintptr_t)stream_list >> 32));

//     mmio_writel(0x80 + pos * 0x20 + 0x08, stats.size);
//     serial_trace("Stream  size: %d\n", sizeof(struct stream_buffer));
//     mmio_writel(0x80 + pos * 0x20 + 0x0C, 1);

//     /* Kirim perintah untuk memulai pemutaran */

//     uint32_t payload = hda_return_sound_data_format(44100, 2, 16);
//     mmio_writel(0x80 + pos * 0x20 + 0x12, payload);

//     send_verb_pio(0, pin_speaker_node, 0x200, payload);
//     usleep(100);

//     // mmio_writel (0x20, 0xFFFF);

//     mmio_writel(0x80 + pos * 0x20 + 0x02, 0x14);
//     mmio_writel(0x80 + pos * 0x20 + 0x0, mmio_readl(0x80 + pos * 0x20 + 0x0) | 0x2);
//     KDEBUG(1, "HDA: playing %s with size %d\n", file_path, stats.size);
// }
