#ifndef __FIRMW__DISPLAY_PORT_H__
#define __FIRMW__DISPLAY_PORT_H__

#include <libk/type.h>

typedef struct edid_data {
  uint8_t padding[8];
  uint16_t manufacturer_id;
  uint16_t product_id;
  uint32_t serial_number;
  uint8_t manufacture_week;
  uint8_t manufacture_year;
  uint8_t edid_version;
  uint8_t edid_revision;
  uint8_t video_input;
  uint8_t max_h_image_size;
  uint8_t max_v_image_size;
  uint8_t display_gamma;
  uint8_t dpms;
  uint16_t chroma : 10;
  uint8_t established_timing[2];
  uint16_t standard_timing[8];
} edid_data_t;

#endif // __FIRMW__DISPLAY_PORT_H__
