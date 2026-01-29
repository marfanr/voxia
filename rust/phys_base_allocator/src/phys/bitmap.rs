use kern_utils::constant::MmapEntryType;
use kern_utils::{align_up, byte_to_gb, ceil_div, debug, serialfmt};

use crate::context::MemoryContext;
use core::alloc::Layout;
use core::cell::UnsafeCell;
use core::ptr::null_mut;

// use kern_types::mem::MemoryManager;

const BLOCK_SIZE: usize = 0x1000;

#[allow(dead_code)]
pub struct BitMap {
    pub base: UnsafeCell<*mut u64>,
    bitmap_count: usize,
    usable_size: usize,
    total_blocks: usize,
}

unsafe impl Sync for BitMap {}

#[allow(dead_code)]
trait BitOperation {
    unsafe fn test(&self, bit: usize) -> bool;
    unsafe fn set(&self, bit: usize);
    unsafe fn clear(&self, bit: usize);
}

impl BitOperation for BitMap {
    #[inline(always)]
    unsafe fn test(&self, bit: usize) -> bool {
        let bitmap = self.as_slice();
        (bitmap[bit >> 6] >> (bit & 63)) & 1 != 0
    }

    #[inline(always)]
    unsafe fn set(&self, bit: usize) {
        let bitmap = self.as_slice();
        bitmap[bit >> 6] |= 1u64 << (bit & 63);
    }

    #[inline(always)]
    unsafe fn clear(&self, bit: usize) {
        let bitmap = self.as_slice();
        bitmap[bit >> 6] &= !(1u64 << (bit & 63));
    }
}

impl BitMap {
    pub const fn init() -> Self {
        Self {
            base: UnsafeCell::new(core::ptr::null_mut()),
            bitmap_count: 0,
            total_blocks: 0,
            usable_size: 0,
        }
    }

    pub fn as_slice(&self) -> &mut [u64] {
        unsafe { core::slice::from_raw_parts_mut(*self.base.get(), self.bitmap_count) }
    }

    pub fn setup(&mut self, ctx: *mut MemoryContext) {
        let ctx = unsafe { &mut *ctx };
        serialfmt!("bitmap found mem entries {} \n", ctx.memory_entries);

        let mut max_addr: usize = 0;
        let mut usable_ram: usize = 0;

        for i in 0..ctx.memory_entries as usize {
            let entry = ctx.memory_map[i];

            if let Some(MmapEntryType::Usable) = MmapEntryType::from_u32(entry.kind) {
                usable_ram += entry.length as usize;
                max_addr = max_addr.max((entry.base + entry.length) as usize);
            }
        }

        serialfmt!("max addr {} Gb \n", byte_to_gb(max_addr as f64));
        serialfmt!("ram_size {} Gb \n", byte_to_gb(usable_ram as f64));

        let total_block = max_addr / BLOCK_SIZE;
        let bitmap_bits = ceil_div(total_block, 8);
        let bitmap_bytes = align_up(bitmap_bits, BLOCK_SIZE);
        let bitmap_words = bitmap_bytes / 8;
        self.bitmap_count = bitmap_words;
        self.total_blocks = total_block;

        serialfmt!(
            "total block {} metadata count {} (size {} kb)\n",
            total_block,
            bitmap_words,
            bitmap_bytes / 1024
        );

        // Creating bitmap
        let mut bitmap_base: Option<u64> = None;

        for i in 0..ctx.memory_entries as usize {
            let e = &mut ctx.memory_map[i];
            if e.kind != MmapEntryType::Usable as u32 {
                continue;
            }

            let aligned = align_up(e.base as usize, BLOCK_SIZE) as u64;
            let used = (aligned - e.base) + bitmap_bytes as u64;

            if e.length >= used {
                bitmap_base = Some(aligned);

                e.base += used;
                e.length -= used;
                break;
            }
        }

        for i in 0..ctx.memory_entries as usize {
            let e = ctx.memory_map[i];

            debug!(
                "addr {:#x} ({}) -- {:#x} ({}) type {} ({} Kb)\n",
                e.base,
                e.base / BLOCK_SIZE as u64,
                e.base + e.length,
                (e.base + e.length) / BLOCK_SIZE as u64,
                e.get_kind_str(),
                e.length / 1024
            );
        }

        let bitmap_addr: u64 = bitmap_base.expect("bitmap base has a value");
        self.base = UnsafeCell::new(bitmap_addr as *mut u64);
        self.usable_size = max_addr;

        // Filling bitmap - mark all as allocated first
        serialfmt!("bitmap base {:#x}\n", bitmap_addr);
        let bitmap = self.as_slice();
        bitmap.fill(!0u64);

        // Mark usable regions as free
        for i in 0..ctx.memory_entries as usize {
            let entry = &ctx.memory_map[i];
            if entry.kind == MmapEntryType::Usable as u32 {
                let start_block = entry.base as usize / BLOCK_SIZE;
                let end_block =
                    align_up(entry.base as usize + entry.length as usize, BLOCK_SIZE) / BLOCK_SIZE;

                serialfmt!(
                    "free blocks {:#x} ({}) to {:#x} ({})\n",
                    entry.base,
                    start_block / 64,
                    entry.base + entry.length,
                    end_block / 64
                );

                for block in start_block..end_block.min(total_block) {
                    unsafe {
                        self.clear(block);
                    }
                }
            }
        }

        serialfmt!("bitmap setup done\n");
    }

    pub unsafe fn alloc(&self, block: usize) -> *mut u8 {
        if block == 0 {
            return null_mut();
        }

        let bitmap = self.as_slice();

        let mut consecutive = 0;
        let mut start_bit = 0;

        for bit in 0..self.total_blocks {
            let word_idx = bit >> 6;
            let bit_offset = bit & 63;

            if bitmap[word_idx] & (1u64 << bit_offset) == 0 {
                // Bit free
                if consecutive == 0 {
                    start_bit = bit;
                }
                consecutive += 1;

                if consecutive == block {
                    // Allocate blocks
                    for b in start_bit..(start_bit + block) {
                        bitmap[b >> 6] |= 1u64 << (b & 63);
                    }

                    return (start_bit * BLOCK_SIZE) as *mut u8;
                }
            } else {
                // Bit allocated, reset
                consecutive = 0;
            }
        }

        null_mut()
    }

    #[allow(dead_code)]
    unsafe fn dealloc(&self, _: *mut u8, _: Layout) {
        todo!("while be implemented");
    }
}
