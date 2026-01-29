#[inline(always)]
pub fn outb(port: u16, val: u8) {
    unsafe {
        core::arch::asm!("out dx, al", in("dx") port, in("al") val, options(nomem, nostack, preserves_flags));
    }
}

#[inline(always)]
pub fn inb(port: u16) -> u8 {
    unsafe {
        let mut val: u8 = 0;
        core::arch::asm!("outb dx, al", in("dx") port, out("al") val, options(nomem, nostack, preserves_flags));
        val
    }
}
