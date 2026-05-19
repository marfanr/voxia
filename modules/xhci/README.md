# XHCI Driver Module

Driver untuk eXtensible Host Controller Interface (USB 3.0) untuk kernel Voxia.

## Fitur yang diimplementasikan:
- Deteksi perangkat PCI XHCI.
- Inisialisasi Capability, Operational, dan Runtime registers.
- Reset controller (HCRST).
- Setup Device Context Base Address Array (DCBAA).
- Setup Command Ring dan Event Ring (Interrupter 0).
- Mengaktifkan controller (Run/Stop bit).
- Deteksi port dan status koneksi (Port Detection).
- Reset port otomatis jika perangkat terdeteksi.
- **Manajemen Slot dan Context**: Mengaktifkan slot perangkat (Enable Slot).
- **Addressing**: Melakukan `Address Device` untuk inisialisasi endpoint kontrol (EP0).
- **Data Transfer**: Fungsi `send_async_with_response` untuk mengirim data (Setup Stage, Data Stage, Status Stage) menggunakan Transfer Ring.
- **Event Handling**: Polling Event Ring untuk `Command Completion` dan `Transfer Event`.

## Daftar File yang Dibuat:
- `manifest.yaml`: Metadata modul dan informasi matching PCI.
- `Makefile`: Script untuk build modul menjadi `.elf`.
- `include/xhci/xhci.hpp`: Definisi struktur register XHCI, TRB, dan kelas `XHCIModule`.
- `src/init.cpp`: Entry point modul, registrasi ke stack USB ioforge, dan loop inisialisasi utama.
- `src/xhci.cpp`: Implementasi logika reset, inisialisasi, dan deteksi port.

## Cara Build:
Jalankan `make` di direktori ini. Hasil akhir berupa `xhci.elf`.

## Catatan Arsitektur:
Modul ini mengikuti pola arsitektur driver Voxia:
1. Mewarisi `IOforgePCI` untuk akses perangkat PCI.
2. Menggunakan `IOUtils` untuk alokasi memori DMA (penting untuk USB controllers).
3. Meregistrasikan diri ke root controller USB di ioforge agar stack USB di atasnya bisa mengenali controller ini.
4. Header sistem diambil dari `../../include/` (seperti `type.h`, `ioforge/`, `memory/`).
