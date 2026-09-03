#ifndef __CPU__CORE_H__
#define __CPU__CORE_H__

#include <type.h>
#include <procc/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t get_current_core_cpuid();
thread_t *get_current_thread(void);

#ifdef __cplusplus
}
#endif


#endif // __CPU_CORE_H__