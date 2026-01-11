#[allow(dead_code)]
use crate::allocator::Allocator;
use core::cell::UnsafeCell;

#[allow(dead_code)]
pub struct BitMap {
    base_start: u64,
    size: usize,
}

#[allow(dead_code)]
pub struct GlobalBitmap {
    inner: UnsafeCell<BitMap>,
}

// WAJIB untuk static global
unsafe impl Sync for GlobalBitmap {}

#[allow(dead_code)]
static BITMAP_STATE: GlobalBitmap = GlobalBitmap {
    inner: UnsafeCell::new(BitMap {
        base_start: 0,
        size: 0,
    }),
};

impl Allocator for GlobalBitmap {
    fn setup(&self, _ctx: *mut crate::context::MemoryContext) {
        // unsafe {
        //     // let state = &mut *self.inner.get();

        // }
    }
}
