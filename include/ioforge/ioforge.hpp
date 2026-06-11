#ifndef __IOFORGE__IOFORGE_HPP__
#define __IOFORGE__IOFORGE_HPP__

#include "ioforge.h"
#include <cpu/core.h>

#define IoForgeModuleConstructor(Class)                                        \
	static Class instance;                                                 \
	extern "C" void load();                                                \
	extern "C" void load() { instance.load(); }

#define log(mod, fmt, ...)                                                     \
	serial2_printf("[INFO][%s][CORE %d] " fmt "\n", mod,                   \
	               (int)get_current_core_cpuid(), ##__VA_ARGS__)

class IOForge {
      public:
	inline IOForge(const char* mod) : mod(mod) {}

	class IOUtils {
	      public:
		// NOTE!: operasi DMA minimal alokasi 1 block (4kb)
		inline static void* DMAAlloc(size_t size, uintptr_t* paddr) {
			return ioforge_dma_alloc(size, paddr);
		}
		inline static void sleep(uint32_t us) { ioforge_sleep(us); }
		inline static void isr_map(uint8_t irq, uint8_t vector) {
			ioforge_map_isr(irq, vector);
		}
		inline static uint16_t irq_alloc_entry() {
			return ioforge_irq_alloc_entry();
		}
		inline static uint16_t irq_alloc_on_core(uint8_t core_id) {
			return ioforge_irq_alloc_on_core(core_id);
		}
		inline static void irq_register(uint8_t n, void* handler) {
			ioforge_irq_register(n, handler);
		}
		inline static void irq_register_on_core(uint8_t core_id,
		                                        uint8_t n,
		                                        void* handler) {
			ioforge_irq_register_on_core(core_id, n, handler);
		}
		inline static uint32_t isr_get_vector(uint8_t irq) {
			return ioforge_isr_get_vector(irq);
		}
		inline static uint8_t get_active_core_count() {
			return ioforge_get_active_core_count();
		}
		inline static void* alloc(size_t size) {
			return ioforge_alloc(size);
		}
		inline static void free(void* ptr, size_t size) {
			ioforge_free(ptr, size);
		}
		inline static void DMAFree(void* paddr, void* vaddr,
		                           size_t size) {
			ioforge_dma_free(paddr, vaddr, size);
		}
		inline static void memset(void* ptr, uint8_t value,
		                          size_t num) {
			ioforge_memset(ptr, value, num);
		}
		inline static void memcpy(void* dst, void* src, size_t num) {
			ioforge_memcpy(dst, src, num);
		}
		inline static void strcopy(char* dst, char* src) {
			IOforgeStrCopy(dst, src);
		}
		inline static void strncopy(char* dst, char* src, size_t len) {
			IOforgeStrnCopy(dst, src, len);
		}
	};

      protected:
	const char* mod;

	//   private:
};

// memory allocation
// void* operator new(size_t size) { return IOForge::IOUtils::alloc(size); }
#endif // __IOFORGE__IOFORGE_HPP__