pub trait AllocatorBackend {
    // fn alloc(&self, size: usize) -> core::result::Result<usize, ()>;
    // fn free(&self, ptr: usize);
}

// use core::alloc::{GlobalAlloc, Layout};

// pub struct KernelAllocator {
//     heap_start: usize,
// }

// unsafe impl GlobalAlloc for KernelAllocator {
//     unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
//         let size = layout.size();
//         {   
//             return 0 as *mut u8;
//         }
//     }

//     unsafe fn dealloc(&self, _: *mut u8, _: Layout) {}
// }
