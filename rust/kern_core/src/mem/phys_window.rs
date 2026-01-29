use core::{
    cell::UnsafeCell,
    mem::MaybeUninit,
    sync::atomic::{AtomicBool, Ordering},
};

use kern_utils::{serialfmt, Global};

use crate::cpu::paging;

pub struct PhysMapWindow {
    pub virt: u64,
    pub phys: u64,
    pub flags: u64,
    pub pml4: Option<*mut u64>,
    pub reserved: AtomicBool,
}

pub const PHYS_MAP_WINDOW_COUNT: usize = 16;

static PHYS_MAP_WINDOWS: Global<MaybeUninit<[PhysMapWindow; PHYS_MAP_WINDOW_COUNT]>> =
    Global(UnsafeCell::new(MaybeUninit::uninit()));

static INIT_PHYS_MAP_WINDOWS: AtomicBool = AtomicBool::new(false);

pub unsafe fn init_phys_map_windows() {
    let window = PHYS_MAP_WINDOWS.0.get();

    (*window).write(core::array::from_fn(|_| PhysMapWindow {
        virt: 0,
        phys: 0,
        flags: 0,
        pml4: None,
        reserved: AtomicBool::new(false),
    }));
    INIT_PHYS_MAP_WINDOWS.store(true, Ordering::Release);
}

pub fn get_phys_map_windows() -> &'static mut [PhysMapWindow; PHYS_MAP_WINDOW_COUNT] {
    assert!(INIT_PHYS_MAP_WINDOWS.load(Ordering::Acquire));
    unsafe { (*PHYS_MAP_WINDOWS.0.get()).assume_init_mut() }
}

impl PhysMapWindow {
    pub fn mark_reserved(&self) {
        self.reserved.store(true, Ordering::Relaxed);
    }

    pub fn mark_unreserved(&self) {
        self.reserved.store(false, Ordering::Release);
    }

    pub fn is_reserved(&self) -> bool {
        self.reserved.load(Ordering::Relaxed)
    }
}

pub fn create_window(
    paddr: u64,
    flags: u64,
    lock: bool,
) -> core::result::Result<&'static mut PhysMapWindow, ()> {
    let windows = get_phys_map_windows();

    // let mut i = 0;
    for window in windows.iter_mut() {
        if !window.is_reserved() {
            let pml4 = paging::get_active_page_root();
            serialfmt!("pml4 {:#p}\n", pml4);
            unsafe {
                paging::paging_phys_window(pml4, window.virt as usize, paddr as usize, flags);
            }
            window.pml4 = Some(pml4);
            window.phys = paddr;
            window.flags = flags;
            if lock {
                window.mark_reserved();
            }
            return Ok(window);
        }
    }

    Err(())
}

pub fn destroy_window(window: &mut PhysMapWindow) {
    window.pml4 = None;
    window.phys = 0;
    window.flags = 0;
    window.mark_unreserved();
}
