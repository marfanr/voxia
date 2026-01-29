use kern_utils::serialfmt;
use phys_base_allocator::context::MemoryContext;

use crate::mem::allocator::GLOBAL_ALLOC;

// static GLOBAL_BITMAP: Global<BitMap> = Global(UnsafeCell::new(BitMap::init()));

#[allow(unused)]
#[no_mangle]
pub extern "C" fn bitmap_setup(ctx: *mut MemoryContext) {
    unsafe {
        let bm = &mut *GLOBAL_ALLOC.bitmap.get();
        bm.setup(ctx);
        serialfmt!("bitmap base at {:#p}\n", *bm.base.get());
    }
}
