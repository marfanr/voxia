#[repr(C)]
#[derive(Clone, Copy)]
pub struct MemoryEntry {
    pub base: u64,
    pub length: u64,
    pub kind: u32,
}

const MAX_MEMORY_ENTRIES: usize = 256;

#[repr(C)]
pub struct MemoryContext {
    pub memory_entries: u32,
    pub memory_map: [MemoryEntry; MAX_MEMORY_ENTRIES],
}