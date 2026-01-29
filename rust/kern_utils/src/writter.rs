use core::ffi::c_char;
use core::fmt::{self, Write};

unsafe extern "C" {
    fn serial_putc(c: c_char);
}

#[inline]
fn putc(c: u8) {
    unsafe {
        serial_putc(c as c_char);
    }
}

struct SerialWriter;

impl Write for SerialWriter {
    fn write_str(&mut self, s: &str) -> fmt::Result {
        for b in s.bytes() {
            putc(b);
        }
        Ok(())
    }
}

pub fn serial_print(args: fmt::Arguments) {
    use core::fmt::Write;
    SerialWriter.write_fmt(args).unwrap();
}

#[macro_export]
macro_rules! serialfmt {
    ($($args:tt)*) => {
        $crate::writter::serial_print(core::format_args!($($args)*))
    };
}

