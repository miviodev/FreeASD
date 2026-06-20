// Raw syscall wrapper — calls ___syscall from libasd syscall_asm.S
// C prototype: long __syscall(long nr, long a0, a1, a2, a3, a4)
// Mach-O: C name __syscall → asm label ___syscall

extern "C" {
    // The C function is `__syscall` → Mach-O symbol `___syscall`.
    // Rust on darwin prepends one `_` to link_name, so link_name = "__syscall"
    // produces the reference `___syscall` which matches the actual symbol.
    #[link_name = "__syscall"]
    fn raw_syscall(nr: i64, a0: i64, a1: i64, a2: i64, a3: i64, a4: i64) -> i64;
}

pub unsafe fn syscall0(nr: i64) -> i64 {
    raw_syscall(nr, 0, 0, 0, 0, 0)
}

pub unsafe fn syscall1(nr: i64, a0: i64) -> i64 {
    raw_syscall(nr, a0, 0, 0, 0, 0)
}

pub unsafe fn syscall2(nr: i64, a0: i64, a1: i64) -> i64 {
    raw_syscall(nr, a0, a1, 0, 0, 0)
}

pub unsafe fn syscall3(nr: i64, a0: i64, a1: i64, a2: i64) -> i64 {
    raw_syscall(nr, a0, a1, a2, 0, 0)
}

pub unsafe fn syscall6(nr: i64, a0: i64, a1: i64, a2: i64, a3: i64, a4: i64) -> i64 {
    raw_syscall(nr, a0, a1, a2, a3, a4)
}

// Syscall numbers (must match kernel/arch/syscall.h)
pub const SYS_EXIT:        i64 = 1;
pub const SYS_OPEN:        i64 = 2;
pub const SYS_CLOSE:       i64 = 3;
pub const SYS_READ:        i64 = 4;
pub const SYS_WRITE:       i64 = 5;
pub const SYS_STAT:        i64 = 6;
pub const SYS_SEEK:        i64 = 7;
pub const SYS_MKDIR:       i64 = 8;
pub const SYS_UNLINK:      i64 = 9;
pub const SYS_READDIR:     i64 = 10;
pub const SYS_SPAWN:       i64 = 11;
pub const SYS_WAIT:        i64 = 12;
pub const SYS_GETPID:      i64 = 13;
pub const SYS_YIELD:       i64 = 14;
pub const SYS_TIME:        i64 = 15;
pub const SYS_GETUID:      i64 = 16;
pub const SYS_UNAME:       i64 = 21;
pub const SYS_GETTIME_NS:  i64 = 27;
pub const SYS_NET_SEND:    i64 = 30;
pub const SYS_NET_RECV:    i64 = 31;

pub fn exit(code: i32) -> ! {
    unsafe { syscall1(SYS_EXIT, code as i64) };
    loop {}
}

pub fn write(fd: i32, buf: &[u8]) -> i64 {
    unsafe { syscall3(SYS_WRITE, fd as i64, buf.as_ptr() as i64, buf.len() as i64) }
}

pub fn read(fd: i32, buf: &mut [u8]) -> i64 {
    unsafe { syscall3(SYS_READ, fd as i64, buf.as_mut_ptr() as i64, buf.len() as i64) }
}

pub fn open(path: &[u8], flags: i32) -> i64 {
    unsafe { syscall2(SYS_OPEN, path.as_ptr() as i64, flags as i64) }
}

pub fn close(fd: i32) -> i64 {
    unsafe { syscall1(SYS_CLOSE, fd as i64) }
}

pub fn getpid() -> i64 {
    unsafe { syscall0(SYS_GETPID) }
}

pub fn yield_cpu() {
    unsafe { syscall0(SYS_YIELD); }
}

pub fn gettime_ns() -> i64 {
    unsafe { syscall0(SYS_GETTIME_NS) }
}
