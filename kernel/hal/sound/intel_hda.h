#ifndef __HAL__SOUND__INTEL_HDA_H__
#define __HAL__SOUND__INTEL_HDA_H__

#include <type.h>

#define INTEL_HDA_GCAP_OFFSET 0x0
#define INTEL_HDA_VMIN_OFFSET 0x1
#define INTEL_HDA_VMAJ_OFFSET 0x2
#define INTEL_HDA_OUTPAY_OFFSET 0x3
#define INTEL_HDA_INPAY_OFFSET 0x4
#define INTEL_HDA_GCTL_OFFSET 0x8
#define INTEL_HDA_WAKEEN_OFFSET 0x0C
#define INTEL_HDA_STATESTS_OFFSET 0x0E
#define INTEL_HDA_INT_OFFSET 0X20

void intel_hda_init();
void intel_hda_play(const char* file_path);

#endif // __HAL__SOUND__INTEL_HDA_H__