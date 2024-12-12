#include "initrd.h"
#include <libk/fs/tar.h>
#include <libk/serial.h>
#include <libk/str/strncmp.h>

/**
 * @brief Mengonversi bilangan oktal menjadi bilangan biner.
 *
 * Fungsi ini mengonversi string yang mewakili bilangan oktal menjadi bilangan
 * biner. Bilangan oktal direpresentasikan sebagai string dengan
 * karakter-karakter '0' hingga '7'. Setiap karakter diubah menjadi bilangan
 * desimal dan kemudian diubah menjadi bilangan biner.
 *
 * @param str Pointer ke string yang mewakili bilangan oktal.
 * @param len Panjang string yang mewakili bilangan oktal.
 * @return Bilangan biner yang dihasilkan dari konversi.
 */
int initrd_oct2bin(unsigned char *str, int len) {
  int n = 0;
  unsigned char *c = str;
  while (len-- > 0) {
    n *= 8;
    n += *c - '0';
    c++;
  }
  return n;
}

/**
 * @brief load file from initrd
 *
 * @param module initrd module
 * @param name file name
 * @return char* file data
 */
char *initrd_load(initrd_module_t module, const char *name) {
  uint8_t *addr = (uint8_t *)module.start;
  TarHeader *header = (TarHeader *)addr;

  while (strncmp(header->ustar, "ustar", 5) == 0) {
    int size = initrd_oct2bin(header->size, 11);
    if (strncmp(header->filename, name, sizeof(name)) == 0) {
      serial_send_string(header->filename);
      serial_send_string(" loaded from rootdir\n");
      if (header->typeflag[0] == '5') { // directory
        uint8_t *subaddr = addr + 512;
        header = (TarHeader *)subaddr;
        serial_trace("file %s loaded from subdir\n", header->filename);
        // continue;
        char *out = subaddr + 512;
        return out;
      }

      char *out = addr + 512;
      header = (TarHeader *)out;
      serial_trace("file %s loaded\n", header->filename);
      // initrd_file_t file = {.name = header->filename,
      //                       .size = oct2bin(header->size, 11),
      //                       .data = (uint8_t *)out};
      // return file;
      return out;
    }
    addr += (((size + 511) / 512) + 1) * 512;
    header = (TarHeader *)addr;
  }
  return 0;
}
