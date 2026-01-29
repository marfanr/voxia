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
