use core::{
    cell::UnsafeCell,
    ptr::NonNull,
    sync::atomic::{AtomicBool, Ordering},
    usize,
};

use kern_utils::{serialfmt, Global};

use crate::mem::{allocator::GLOBAL_ALLOC, phys_window};

pub type PageT = *mut u64;

#[doc = "PML4 PHYS MEMORY"]
static PAGE_ROOT: Global<PageT> = Global::new(core::ptr::null_mut());
static PAGING_HAS_BEEN_SET: AtomicBool = AtomicBool::new(false);
static PHYS_WINDOW_PT: Global<Option<u64>> = Global::new(None);

pub fn create_page_directory() -> NonNull<u64> {
    unsafe {
        let bitmap = &mut *GLOBAL_ALLOC.bitmap.get();
        let page = bitmap.alloc(1) as *mut u64;

        let paging_has_set = PAGING_HAS_BEEN_SET.load(Ordering::Relaxed);
        if paging_has_set {
            serialfmt!("paging has been set\n");
            let window = phys_window::create_window(0x1000u64, 0b11, false);
            // let vaddr = window.virt as *mut u64;
            // core::ptr::write_bytes(vaddr, 0, 4096);

            //     return NonNull::new(vaddr).expect("page ptr null");
            // phys_window::create_window(2u64, 0b11, false);
        }

        core::ptr::write_bytes(page, 0, 4096);

        NonNull::new(page).expect("page ptr null")
    }
}

pub fn get_page_root() -> PageT {
    unsafe { *PAGE_ROOT.0.get() }
}

pub fn get_active_page_root() -> PageT {
    unsafe {
        let mut cr3: u64;
        core::arch::asm!(
            "mov {}, cr3",
            out(reg) cr3,
            options(nomem, nostack, preserves_flags),
        );
        cr3 as PageT
    }
}

// TODO! will be move later
const PHYS_WINDOW_START: usize = 0xFFFF_B000_0000_0000;
const PHYS_WINDOW_PT_ADDR: usize = 0xFFFF_B000_0F00_0000;
const BITMAP_ADDR: usize = 0xFFFF_FFE0_0000_0000;

pub unsafe fn write_cr3(cr3: u64) {
    core::arch::asm!(
        "mov cr3, {}",
        in(reg) cr3,
        options(nomem, nostack, preserves_flags),
    );
}

#[doc = "utility to access phys memory without identity mapping"]
pub unsafe fn paging_phys_window(pml4: *mut u64, virt: usize, phys: usize, flags: u64) {
    let index1 = (virt >> 12) & 0x1ff;
    let phys_window_pt = &mut *PHYS_WINDOW_PT.0.get();

    if let None = phys_window_pt {
        let idx2 = (virt >> 21) & 0x1ff;
        let idx3 = (virt >> 30) & 0x1ff;
        let idx4 = (virt >> 39) & 0x1ff;

        // PML4E
        let pml4e = pml4.add(idx4);
        let pdpt = if (*pml4e & 1) != 0 {
            (*pml4e & 0x000f_ffff_ffff_f000) as *mut u64
        } else {
            let p = create_page_directory().as_ptr();
            *pml4e = (p as u64) | flags;
            p
        };

        // PDPT
        let pdpte = pdpt.add(idx3);
        let pd = if (*pdpte & 1) != 0 {
            (*pdpte & 0x000f_ffff_ffff_f000) as *mut u64
        } else {
            let p = create_page_directory().as_ptr();
            *pdpte = (p as u64) | flags;
            p
        };

        // PD
        let pde = pd.add(idx2);
        let pt = if (*pde & 1) != 0 {
            (*pde & 0x000f_ffff_ffff_f000) as *mut u64
        } else {
            let p = create_page_directory().as_ptr();
            *pde = (p as u64) | flags;
            p
        };
        *phys_window_pt = Some(pt as u64);
    };

    serialfmt!(
        "phys window pt {:#X}\n",
        phys_window_pt.expect("phys window pt not set")
    );

    let pt_ptr = phys_window_pt.expect("phys window pt not set") as *mut u64;

    let pte = pt_ptr.add(index1);
    *pte = (phys as u64 & 0x000f_ffff_ffff_f000) | flags | 1;

    // flush
    paging_flush(virt);
}

unsafe fn initialize_physical_paging_window() {
    phys_window::init_phys_map_windows();
    let pml4: &mut *mut u64 = &mut *PAGE_ROOT.0.get();

    let phys_window = phys_window::get_phys_map_windows();
    for i in 0..phys_window::PHYS_MAP_WINDOW_COUNT as usize {
        mmap(*pml4, (PHYS_WINDOW_START + i * 0x1000) as usize, 0, 0b11);
        phys_window[i].virt = PHYS_WINDOW_START as u64 + (i as u64 * 0x1000);
        phys_window[i].phys = 0;
        phys_window[i].flags = 0;
        paging_phys_window(
            *pml4,
            phys_window[i].virt as usize,
            phys_window[i].phys as usize,
            0b11,
        );
    }

    // TODO: change PHYS_WINDOW_PT to virt addr
    let pt = &mut *PHYS_WINDOW_PT.0.get();

    mmap(
        *pml4,
        PHYS_WINDOW_PT_ADDR,
        pt.expect("phys window pt not set") as usize,
        0b11,
    );
    *pt = Some(PHYS_WINDOW_PT_ADDR as u64);

    serialfmt!("phys window initialized\n");
}

pub unsafe fn paging_setup() {
    let pml4 = &mut *PAGE_ROOT.0.get();
    *pml4 = create_page_directory().as_ptr();
    serialfmt!("pml4 {:#p} {:#x}\n", *pml4, *pml4 as u64);

    initialize_physical_paging_window();

    for i in (0usize..0x8000_0000usize).step_by(0x1000) {
        mmap(*pml4, i + 0xffff_ffff_8000_0000usize, i, 0b11);
    }

    // change bitmap base
    {
        let bitmap = &mut *GLOBAL_ALLOC.bitmap.get();
        let bitmap_base = bitmap.base.get();
        mmap(*pml4, BITMAP_ADDR, bitmap_base as usize, 0b11);
        bitmap.base = UnsafeCell::new(BITMAP_ADDR as *mut u64);
    }

    // reload
    write_cr3(*pml4 as u64);

    PAGING_HAS_BEEN_SET.store(true, Ordering::Relaxed);

    // test
    let window = phys_window::create_window(0x1000u64, 0b11, false).expect("give a valid vaddr");
    serialfmt!("window created on {:#x}\n", window.virt);
}

pub unsafe fn mmap(pml4: *mut u64, virt: usize, phys: usize, flags: u64) {
    let idx1 = (virt >> 12) & 0x1ff;
    let idx2 = (virt >> 21) & 0x1ff;
    let idx3 = (virt >> 30) & 0x1ff;
    let idx4 = (virt >> 39) & 0x1ff;

    // PML4
    // let pml4_virt: u64 = if *PAGING_HAS_BEEN_SET.as_ptr() {
    //     let window = phys_window::create_window(pml4 as u64, flags, false)
    //         .expect("phys window is available");
    //     window.virt
    // } else {
    //     pml4 as u64
    // };

    let pml4e = pml4.add(idx4);

    let pdpt = if (*pml4e & 1) != 0 {
        (*pml4e & 0x000f_ffff_ffff_f000) as *mut u64
    } else {
        let p = create_page_directory().as_ptr();
        *pml4e = (p as u64) | flags;
        p
    };

    // PDPT
    let pdpte = pdpt.add(idx3);
    let pd = if (*pdpte & 1) != 0 {
        (*pdpte & 0x000f_ffff_ffff_f000) as *mut u64
    } else {
        let p = create_page_directory().as_ptr();
        *pdpte = (p as u64) | flags;
        p
    };

    // PD
    let pde = pd.add(idx2);
    let pt = if (*pde & 1) != 0 {
        (*pde & 0x000f_ffff_ffff_f000) as *mut u64
    } else {
        let p = create_page_directory().as_ptr();
        *pde = (p as u64) | flags;
        p
    };

    // PTE
    let pte = pt.add(idx1);
    *pte = (phys as u64 & 0x000f_ffff_ffff_f000) | flags | 1;
}

pub unsafe fn paging_init_mmap(page: PageT, virt: usize, phys: usize, flags: u64) {
    let idx1 = (virt >> 12) & 0x1ff;
    let idx2 = (virt >> 21) & 0x1ff;
    let idx3 = (virt >> 30) & 0x1ff;
    let idx4 = (virt >> 39) & 0x1ff;

    let pml4 = page;
    let pdpt = create_page_directory().as_ptr();

    let pml4e = pml4.add(idx4);
    if (*pml4e & 1) == 0 {
        // HARUS return phys addr
        *pml4e = (pdpt as u64) | flags | 1;
    }

    let pdpte = pdpt.add(idx3);
    let pdp = create_page_directory().as_ptr();
    if (*pdpte & 1) == 0 {
        *pdpte = (pdp as u64) | flags | 1;
    }

    let pdpe = pdp.add(idx2);
    let pt = create_page_directory().as_ptr();
    if (*pdpe & 1) == 0 {
        *pdpe = (pt as u64) | flags | 1;
    }

    let pte = pt.add(idx1);
    *pte = (phys as u64) | flags | 1;

    // flush
    paging_flush(virt);

    // let
}

pub unsafe fn paging_flush(virt: usize) {
    core::arch::asm!("invlpg [{}]", in(reg) virt, options(nostack, preserves_flags));
}
