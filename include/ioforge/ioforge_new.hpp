#ifndef __IOFORGE__IOFORGE_NEW_HPP__
#define __IOFORGE__IOFORGE_NEW_HPP__

#include <type.h>

inline void* operator new(size_t, void* ptr) noexcept {
	return ptr;
}
inline void* operator new[](size_t, void* ptr) noexcept {
	return ptr;
}
inline void operator delete(void*, void*) noexcept {
}
inline void operator delete[](void*, void*) noexcept {
}

#endif // __IOFORGE__IOFORGE_NEW_HPP__