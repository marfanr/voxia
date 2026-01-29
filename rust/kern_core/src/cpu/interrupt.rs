use core::arch::asm;
use core::ffi::c_void;
use kern_utils::io::outb;

const VOXIA_MAX_CORE: usize = 64;
const MAX_INTERRUPTS: usize = 256;
const PIC1: u16 = 0x20; /* IO base address for master PIC */
const PIC2: u16 = 0xA0; /* IO base address for slave PIC */
const PIC1_COMMAND: u16 = PIC1;
const PIC1_DATA: u16 = PIC1 + 1;
const PIC2_COMMAND: u16 = PIC2;
const PIC2_DATA: u16 = PIC2 + 1;

const ICW1_ICW4: u8 = 0x01; /* Indicates that ICW4 will be present */
// const ICW1_SINGLE: u8 = 0x02; /* Single (cascade) mode */
// const ICW1_INTERVAL4: u8 = 0x04; /* Call address interval 4 (8) */
// const ICW1_LEVEL: u8 = 0x08; /* Level triggered (edge) mode */
const ICW1_INIT: u8 = 0x10; /* Initialization - required! */
const ICW4_8086: u8 = 0x01; /* 8086/88 (MCS-80/85) mode */
// const ICW4_AUTO: u8 = 0x02; /* Auto (normal) EOI */
// const ICW4_BUF_SLAVE: u8 = 0x08; /* Buffered mode/slave */
// const ICW4_BUF_MASTER: u8 = 0x0C; /* Buffered mode/master */
// const ICW4_SFNM: u8 = 0x10; /* Special fully nested (not) */
pub const INTERRUPT_ATTR_KERNEL: u8 = 0x8E;
pub const INTERRUPT_ATTR_USER: u8 = 0xEE;

#[repr(C, packed)]
#[derive(Copy, Clone)]
pub struct InterruptEntry {
    pub offset_low: u16,
    pub selector: u16,
    pub ist: u8,
    pub type_attr: u8,
    pub offset_mid: u16,
    pub offset_high: u32,
    pub zero: u32,
}

#[repr(C, packed)]
pub struct InterruptPointer {
    pub limit: u16,
    pub base: u64,
}

#[repr(C)]
pub struct IrqEntry {
    pub handler: *mut c_void,
    pub use_default_isr: bool,
    pub configured: bool,
}

#[repr(C)]
pub struct InterruptPerCoreData {
    pub interrupt_entries: [InterruptEntry; MAX_INTERRUPTS],
    pub irq_entries: [IrqEntry; MAX_INTERRUPTS],
    pub interrupt_pointer: InterruptPointer,
}

#[no_mangle]
pub static mut INTERRUPT_PER_CORE_DATA: [InterruptPerCoreData; VOXIA_MAX_CORE] =
    unsafe { core::mem::zeroed() };

#[inline(always)]
pub fn interrupt_io_wait() {
    outb(0x80, 0);
}

unsafe fn interrupt_pic_remap() {
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    interrupt_io_wait();

    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    interrupt_io_wait();

    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);
    interrupt_io_wait();

    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

pub unsafe fn interrupt_reload(ptr: &mut InterruptPointer, tbl: *const InterruptEntry) {
    ptr.limit = (MAX_INTERRUPTS * core::mem::size_of::<InterruptEntry>() - 1) as u16;
    ptr.base = tbl as u64;

    let idtr_ptr: *const InterruptPointer = ptr as *const _;

    asm!(
        "lidt [{}]",
        in(reg) idtr_ptr,
        options(nostack)
    );
}

extern "C" {
    static int_table: [*const (); MAX_INTERRUPTS];
}

pub unsafe fn interrupt_register(
    entries: &mut [InterruptEntry; MAX_INTERRUPTS],
    n: u8,
    handler: *const (),
    selector: u16,
    ist: u8,
    type_attr: u8,
) {
    let addr = handler as u64;

    entries[n as usize] = InterruptEntry {
        offset_low: addr as u16,
        selector,
        ist,
        type_attr,
        offset_mid: (addr >> 16) as u16,
        offset_high: (addr >> 32) as u32,
        zero: 0,
    };
}

pub unsafe fn irq_register(
    core: usize,
    n: u8,
    handler: *const (),
    use_default_isr: bool,
    selector: u16,
    ist: u8,
    type_attr: u8,
) {
    let irq = &mut INTERRUPT_PER_CORE_DATA[core].irq_entries[n as usize];
    irq.handler = handler as *mut _;
    irq.use_default_isr = use_default_isr;
    irq.configured = true;

    let h = if use_default_isr {
        int_table[n as usize]
    } else {
        handler
    };

    interrupt_register(
        &mut INTERRUPT_PER_CORE_DATA[core].interrupt_entries,
        n,
        h,
        selector,
        ist,
        type_attr,
    );
}

pub unsafe fn irq_setup(core: usize) {
    unsafe {
        for i in 0..MAX_INTERRUPTS {
            interrupt_register(
                &mut INTERRUPT_PER_CORE_DATA[core].interrupt_entries,
                i as u8,
                int_table[i],
                0x28,
                0,
                INTERRUPT_ATTR_KERNEL,
            );
        }

        interrupt_pic_remap();

        interrupt_reload(
            &mut INTERRUPT_PER_CORE_DATA[core].interrupt_pointer,
            INTERRUPT_PER_CORE_DATA[core].interrupt_entries.as_ptr(),
        );
        // setup interrupt
    }
}

pub unsafe fn interrupt_enable() {
    unsafe {
        asm!("sti");
    }
}

pub unsafe fn interrupt_setup() {
    unsafe {
        irq_setup(0);
        interrupt_enable();
    }
}
