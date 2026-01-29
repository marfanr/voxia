use core::{
    alloc::{GlobalAlloc, Layout},
    cell::UnsafeCell,
    ptr::null_mut,
};

use phys_base_allocator::phys::bitmap::BitMap;

pub struct KernelAllocator {
    pub bitmap: UnsafeCell<BitMap>,
}

unsafe impl Sync for KernelAllocator {}

unsafe impl GlobalAlloc for KernelAllocator {
    unsafe fn alloc(&self, _layout: Layout) -> *mut u8 {
        // match &mut *self.bitmap.get() {
        //     Some(bm) => bm.alloc(layout),
        //     None => null_mut(),
        // }
        null_mut()
    }

    unsafe fn dealloc(&self, _: *mut u8, _: Layout) {
        // if let Some(bm) = &mut *self.bitmap.get() {
        //     bm.free(ptr, layout.size());
        // }
    }
}

#[global_allocator]
pub static GLOBAL_ALLOC: KernelAllocator = KernelAllocator {
    bitmap: UnsafeCell::new(BitMap::init()),
};

// #[inline(always)]
// pub unsafe fn global_bitmap() -> &'static mut BitMap {
//     let ptr = *GLOBAL_ALLOC.bitmap.get();
//     assert!(!ptr.is_null());
//     &mut *ptr
// }
