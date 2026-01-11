use kern_utils::writter::serial_print;
use phys_base_allocator::context::MemoryContext;

#[allow(unused)]
#[no_mangle]
pub extern "C" fn bitmap_setup(ctx: *mut MemoryContext) {
    // BITMAP_STATE.setup(base, size);
    let ctx = unsafe {&mut *ctx};
    serial_print(core::format_args!("bitmap found mem entries {} \n", ctx.memory_entries));
}