use crate::context::MemoryContext;

#[allow(dead_code)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum AllocError {
    OutOfMemory = 1,
    InvalidLayout = 2,
    InvalidAlign = 3,
    AddressOverflow = 4,
}

impl AllocError {
    #[inline]
    #[allow(dead_code)]
    pub const fn to_errno(self) -> i32 {
        match self {
            AllocError::OutOfMemory => -12,   // ENOMEM
            AllocError::InvalidLayout => -22, // EINVAL
            AllocError::InvalidAlign => -22,
            AllocError::AddressOverflow => -75, // EOVERFLOW
        }
    }
}

pub trait Allocator {
    #[allow(dead_code)]
    fn setup(&self, ctx: *mut MemoryContext);
}

// use core::ptr::null_mut;
// // use kern_utils::writter::serial_print;

// #[repr(C)]
// pub struct Allocator {
//     base: usize,
//     size: usize,
// }

// impl Allocator {
//     pub const fn new(base: usize, size: usize) -> Self {
//         Allocator { base, size }
//     }

//     pub fn alloc(&mut self, layout: core::alloc::Layout) -> *mut u8 {
//         let align = layout.align();
//         let size = layout.size();

//         let aligned_base = (self.base + align - 1) & !(align - 1);

//         if aligned_base + size > self.base + self.size {
//             null_mut()
//         } else {
//             self.base = aligned_base + size;
//             aligned_base as *mut u8
//         }
//     }
// }

// #[no_mangle]
// pub extern "C" fn allocator_new(base: usize, size: usize) -> Allocator {
//     Allocator::new(base, size)
// }
