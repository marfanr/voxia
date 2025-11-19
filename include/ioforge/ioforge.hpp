#ifndef __IOFORGE__IOFORGE_HPP__
#define __IOFORGE__IOFORGE_HPP__

#include "type.h"

#define IoForgeModuleConstructor(Class)                                                            \
    static Class    instance;                                                                      \
    extern "C" void load()                                                                         \
    {                                                                                              \
        instance.load();                                                                           \
    }

extern "C" void     serial_printf(const char *fmt, ...);
extern "C" uint16_t coreGetCpuID();
#define log(mod, fmt, ...)                                                                         \
    serial_printf("[INFO][%s][CORE %d] " fmt "\n", mod, coreGetCpuID(), ##__VA_ARGS__)

class IOForge
{
  public:
    IOForge(const char *mod);

    class IOUtils
    {
      public:
        static void     *DMAAlloc(size_t size, uintptr_t *paddr);
        static void      DMAFree(void *paddr, void *vaddr, size_t size);
        static void     *alloc(size_t size);
        static void      memset(void *ptr, uint8_t value, size_t num);
        static void      memcpy(void *dst, void *src, size_t num);
        static void      sleep(uint32_t us);
        static void      irq_register(uint8_t n, void *handler);
        static void      isr_map(uint8_t irq, uint8_t vector);
        static void      strcopy(char *dst, char *src);
        static uintptr_t mMapPhys(uintptr_t paddr, size_t size);
    };

  protected:
    const char *mod;

  private:
};

// memory allocation
void *operator new(size_t size);
#endif // __IOFORGE__IOFORGE_HPP__