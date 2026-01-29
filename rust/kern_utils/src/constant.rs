#[allow(dead_code)]
#[repr(u32)]
#[derive(Debug, Copy, Clone)]
pub enum MmapEntryType {
    Usable = 1,
    Reserved = 2,
    AcpiReclaimable = 3,
    AcpiNvs = 4,
    BadMemory = 5,
    BootloaderReclaimable = 6,
    KernelAndModules = 7,
    Framebuffer = 8,
}

impl MmapEntryType {
    pub fn from_u32(v: u32) -> Option<Self> {
        match v {
            1 => Some(Self::Usable),
            2 => Some(Self::Reserved),
            3 => Some(Self::AcpiReclaimable),
            4 => Some(Self::AcpiNvs),
            5 => Some(Self::BadMemory),
            6 => Some(Self::BootloaderReclaimable),
            7 => Some(Self::KernelAndModules),
            8 => Some(Self::Framebuffer),
            _ => None,
        }
    }
}
