use core::mem::size_of;

#[repr(C, packed)]
#[derive(Copy, Clone)]
pub struct GdtEntry {
    pub limit_low: u16,
    pub base_low: u16,
    pub base_middle: u8,
    pub access: u8,
    pub flags: u8,
    pub base_high: u8,
}

#[repr(C, packed)]
pub struct GdtPtr {
    pub limit: u16,
    pub base: u64,
}

#[repr(C, packed)]
pub struct LmTss {
    pub reserved: u32,
    pub rsp: [u64; 3],
    pub reserved2: u64,
    pub ist: [u64; 7],
    pub reserved3: u64,
    pub reserved4: u16,
    pub iomap_base: u16,
}

#[no_mangle]
static mut GDT_ENTRIES: [GdtEntry; 14] = [GdtEntry {
    limit_low: 0,
    base_low: 0,
    base_middle: 0,
    access: 0,
    flags: 0,
    base_high: 0,
}; 14];

#[no_mangle]
static mut GDT_PTR: GdtPtr = GdtPtr { limit: 0, base: 0 };

#[no_mangle]
static mut TSS: LmTss = LmTss {
    reserved: 0,
    rsp: [0; 3],
    reserved2: 0,
    ist: [0; 7],
    reserved3: 0,
    reserved4: 0,
    iomap_base: 0,
};

#[repr(align(16))]
#[allow(dead_code)]
struct AlignedStack([u8; 4096]);

#[no_mangle]
static mut STACK: AlignedStack = AlignedStack([0; 4096]);

#[inline(always)]
fn create_entry(base: u32, limit: u16, access: u8, flags: u8) -> GdtEntry {
    GdtEntry {
        limit_low: limit,
        base_low: (base & 0xFFFF) as u16,
        base_middle: ((base >> 16) & 0xFF) as u8,
        access,
        flags,
        base_high: ((base >> 24) & 0xFF) as u8,
    }
}

extern "C" {
    fn reloadGDT(cs: u16, ds: u16);
}

#[inline(always)]
unsafe fn gdt_flush(ptr: *const GdtPtr) {
    core::arch::asm!(
        "lgdt [{}]",
        in(reg) ptr,
        options(readonly, nostack, preserves_flags)
    );
}

#[no_mangle]
pub unsafe extern "C" fn gdt_init() {
    // null
    GDT_ENTRIES[0] = create_entry(0, 0, 0, 0);

    // 16-bit
    GDT_ENTRIES[1] = create_entry(0, 0xFFFF, 0x9A, 0x00);
    GDT_ENTRIES[2] = create_entry(0, 0xFFFF, 0x92, 0x00);

    // 32-bit
    GDT_ENTRIES[3] = create_entry(0, 0xFFFF, 0x9A, 0xCF);
    GDT_ENTRIES[4] = create_entry(0, 0xFFFF, 0x92, 0xCF);

    // 64-bit ring 0
    GDT_ENTRIES[5] = create_entry(0, 0, 0x9A, 0x20);
    GDT_ENTRIES[6] = create_entry(0, 0, 0x92, 0x00);

    // 64-bit ring 3
    GDT_ENTRIES[7] = create_entry(0, 0, 0xFA, 0x20);
    GDT_ENTRIES[8] = create_entry(0, 0, 0xF2, 0x00);

    // ---- TSS ----
    let tss_addr = &raw const TSS as *const _ as u64;
    let tss_size = size_of::<LmTss>() as u16;

    // low
    GDT_ENTRIES[11] = GdtEntry {
        limit_low: tss_size,
        base_low: (tss_addr & 0xFFFF) as u16,
        base_middle: ((tss_addr >> 16) & 0xFF) as u8,
        access: 0xE9,
        flags: 0,
        base_high: ((tss_addr >> 24) & 0xFF) as u8,
    };

    // high
    GDT_ENTRIES[12] = GdtEntry {
        limit_low: ((tss_addr >> 32) & 0xFFFF) as u16,
        base_low: ((tss_addr >> 48) & 0xFFFF) as u16,
        base_middle: 0,
        access: 0,
        flags: 0,
        base_high: 0,
    };

    GDT_PTR.limit = (size_of::<GdtEntry>() * 14 - 1) as u16;
    GDT_PTR.base = &raw const GDT_ENTRIES as *const _ as u64;

    gdt_flush(&raw const GDT_PTR);

    let stack_top = (&raw const STACK as *const _ as u64) + 4096;
    TSS.rsp[0] = stack_top;

    // load TSS (selector 0x58)
    core::arch::asm!(
        "ltr ax",
        in("ax") 0x58u16,
        options(nostack, preserves_flags)
    );

    reloadGDT(0x28, 0x30);
}