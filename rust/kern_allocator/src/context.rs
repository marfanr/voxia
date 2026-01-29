use kern_utils::constant::MmapEntryType;

#[repr(C, align(32))]
#[derive(Debug, Copy, Clone)]
pub struct MemoryEntry {
    pub base: u64,
    pub length: u64,
    pub kind: u32,
}

impl MemoryEntry {
    pub const fn get_kind_str(&self) -> &'static str {
        match self.kind {
            x if x == MmapEntryType::Usable as u32 => "Usable",
            x if x == MmapEntryType::Reserved as u32 => "Reserved",
            x if x == MmapEntryType::AcpiReclaimable as u32 => "ACPI Reclaimable",
            x if x == MmapEntryType::AcpiNvs as u32 => "ACPI NVS",
            x if x == MmapEntryType::BadMemory as u32 => "Bad Memory",
            x if x == MmapEntryType::BootloaderReclaimable as u32 => "Bootloader Reclaimable",
            _ => "Unknown",
        }
    }
}

const MAX_MEMORY_ENTRIES: usize = 256;

#[repr(C)]
pub struct MemoryContext {
    pub memory_entries: u32,
    pub memory_map: [MemoryEntry; MAX_MEMORY_ENTRIES],
}
