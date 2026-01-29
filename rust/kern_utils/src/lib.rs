#![no_std]

use core::cell::UnsafeCell;
pub mod constant;
pub mod io;
pub mod writter;

pub fn byte_to_mb(n: f64) -> f64 {
    n / (1024.0 * 1024.0)
}

pub fn byte_to_gb(n: f64) -> f64 {
    byte_to_mb(n) / 1024.0
}

#[inline(always)]
pub fn align_up(n: usize, align: usize) -> usize {
    (n + align - 1) & !(align - 1)
}

#[inline(always)]
pub fn ceil_div(a: usize, b: usize) -> usize {
    (a + b - 1) / b
}

#[cfg(feature = "debug")]
#[macro_export]
macro_rules! debug {
    ($($arg:tt)*) => {
        serialfmt!("[DEBUG] {}", format_args!($($arg)*));
    };
}

#[cfg(not(feature = "debug"))]
#[macro_export]
macro_rules! debug {
    ($($arg:tt)*) => {};
}

#[macro_export]
macro_rules! error {
    ($($arg:tt)*) => {
        serialfmt!("[ERROR] {}", format_args!($($arg)*));
    };
}

#[repr(transparent)]
pub struct Global<T>(pub UnsafeCell<T>);

unsafe impl<T> Sync for Global<T> {}

impl<T> Global<T> {
    pub const fn new(value: T) -> Self {
        Self(UnsafeCell::new(value))
    }

    #[inline(always)]
    pub fn as_ptr(&self) -> *mut T {
        self.0.get()
    }
}
