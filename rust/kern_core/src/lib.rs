#![no_std]
pub mod cpu;
pub mod mem;

use crate::cpu::{gdt::gdt_init, interrupt::interrupt_setup, paging};
use kern_utils::{debug, error, serialfmt};

extern crate alloc;

// use alloc::boxed::Box;

#[allow(unused)]
#[no_mangle]
extern "C" fn rust_init() {
    // let _ = bitmap::BitMap::init();

    unsafe {
        gdt_init();
        interrupt_setup();
        serialfmt!("interrupt has been setup\n");
        paging::paging_setup();
        serialfmt!("paging has been installed\n");
        // todo!(paging)
    }

    serialfmt!("hello world\n");

    // _ = Box::new(42u64);
    // let x = Box::new(42u64);
    // serialfmt!("x {:#p}\n", &*x as *const u64);
}

#[panic_handler]
fn panic(info: &core::panic::PanicInfo) -> ! {
    error!("KERNEL PANIC!\n");
    if let Some(location) = info.location() {
        error!("at {}:{} : ", location.file(), location.line());
    }

    let msg = info.message();
    serialfmt!("{}\n", msg);

    loop {
        unsafe {
            core::arch::asm!("hlt");
        }
    }
}
