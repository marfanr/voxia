#![no_std]
pub mod cpu;
pub mod mem;

use kern_utils::writter::serial_print;

use crate::cpu::gdt::gdt_init;
// use phys_base_allocator::allocator::*;
// use phys_base_allocator::phys::bitmap;

#[allow(unused)]
#[no_mangle]
extern "C" fn rust_init() {
    // let _ = bitmap::BitMap::init();
    unsafe { gdt_init() };
    serial_print(core::format_args!("hello word"));
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
