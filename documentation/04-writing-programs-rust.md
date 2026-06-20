# Writing Programs in Rust

OpenASD supports `no_std` Rust programs through the `libasd_sys` crate. The crate provides raw syscall wrappers; you build higher-level abstractions on top.

---

## Prerequisites

```sh
# Install rustup (if not already installed)
curl https://sh.rustup.rs -sSf | sh

# Add the required target
rustup target add x86_64-apple-darwin

# Install ld64.lld (usually comes with LLVM/clang)
```

---

## Cargo configuration

Create a `.cargo/config.toml` in your crate root to disable SSE (required — kernel ABI does not preserve SSE registers):

```toml
[target.x86_64-apple-darwin]
rustflags = ["-C", "target-feature=-sse,-sse2,-avx,-avx2"]
```

---

## Cargo.toml

```toml
[package]
name    = "my-asd-program"
version = "1.0.0"
edition = "2021"

[lib]
name      = "my_asd_program"
crate-type = ["staticlib"]

[profile.release]
panic    = "abort"
opt-level = "s"
lto      = true
```

Use `crate-type = ["staticlib"]` — the output `.a` is linked by `ld64.lld` together with `libasd` (C) which provides `_start` and the syscall assembly stub.

---

## Minimal program

```rust
// src/lib.rs
#![no_std]
#![no_main]

// Provide libasd_sys via extern
extern crate asd_sys;
use asd_sys::syscall;

// Panic handler required for no_std
#[panic_handler]
fn panic(_info: &core::panic::PanicInfo) -> ! {
    unsafe { syscall::exit(1) }
}

// Main entry — called from start.S (via C _start)
#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, _envp: *const *const u8) -> i32 {
    let msg = b"hello from Rust!\n";
    unsafe { syscall::write(1, msg) };
    0
}
```

---

## Using the asd_sys crate

Point your Cargo.toml at the local copy:
```toml
[dependencies]
asd_sys = { path = "/path/to/OpenASD/userland/libasd/rust/asd_sys" }
```

### Available functions

```rust
use asd_sys::syscall;

// Process
syscall::exit(code: i32) -> !
syscall::getpid() -> i64
syscall::yield_cpu()

// I/O
syscall::write(fd: i32, buf: &[u8]) -> i64    // returns bytes written
syscall::read(fd: i32, buf: &mut [u8]) -> i64  // returns bytes read

// Files
syscall::open(path: &[u8], flags: i32) -> i64  // fd or negative
syscall::close(fd: i32) -> i64

// Time
syscall::gettime_ns() -> i64   // nanoseconds since boot

// Network
// SYS_NET_SEND = 30, SYS_NET_RECV = 31 (use syscallN directly)
```

### Raw syscalls for everything else

For syscalls not yet wrapped in `asd_sys`, use the raw interface:

```rust
use asd_sys::syscall::{syscall1, syscall2, syscall3, syscall6,
                       SYS_MKDIR, SYS_UNLINK, SYS_STAT};

// mkdir("/mydir")
unsafe { syscall1(SYS_MKDIR, b"/mydir\0".as_ptr() as i64) };

// open for write
unsafe { syscall2(SYS_OPEN, b"/out.txt\0".as_ptr() as i64, 0x12) }; // O_WRONLY|O_CREAT
```

---

## Linking

The Rust crate produces a static library (`libmy_asd_program.a`). Link it with `ld64.lld` the same way C programs are linked:

```makefile
CARGO = ~/.cargo/bin/cargo
TARGET = x86_64-apple-darwin
LIBASD = userland/libasd

build: src/lib.rs Cargo.toml
	PATH="$(dir $(CARGO)):$(PATH)" $(CARGO) build --target $(TARGET) --release
	ld64.lld -arch x86_64 \
	         -platform_version macos 13.0 13.0 \
	         -e _start \
	         $(LIBASD)/build/start.o \
	         target/$(TARGET)/release/libmy_asd_program.a \
	         userland/libasd/rust/asd_sys/target/$(TARGET)/release/libasd_sys.a \
	         $(LIBASD)/build/libasd.a \
	         -o my-program
```

The link order matters:
1. `start.o` — provides `_start` (calls `main`)
2. Your `.a` — provides `main`
3. `libasd_sys.a` — provides `__syscall` Rust wrappers
4. `libasd.a` — provides `__syscall` assembly stub + C utilities

---

## A complete example: ls in Rust

```rust
#![no_std]
#![no_main]

extern crate asd_sys;
use asd_sys::syscall::{syscall2, syscall4, write, exit,
                       SYS_READDIR, SYS_OPEN};

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    unsafe { exit(101) }
}

// asd_dirent_t layout (must match C definition)
#[repr(C)]
struct Dirent {
    name: [u8; 256],
    kind: u8,
    size: i64,
}

#[no_mangle]
pub extern "C" fn main(_argc: i32, _argv: *const *const u8, _envp: *const *const u8) -> i32 {
    let path = b"/\0";
    let mut entries = [Dirent { name: [0; 256], kind: 0, size: 0 }; 64];
    let mut count: u32 = 0;

    let r = unsafe {
        syscall4(SYS_READDIR,
                 path.as_ptr() as i64,
                 entries.as_mut_ptr() as i64,
                 64,
                 &mut count as *mut u32 as i64)
    };

    if r != 0 {
        unsafe { write(2, b"readdir failed\n") };
        return 1;
    }

    for i in 0..count as usize {
        let e = &entries[i];
        let kind: u8 = if e.kind == 2 { b'd' } else { b'-' };
        unsafe { write(1, &[kind, b' ']) };

        let len = e.name.iter().position(|&b| b == 0).unwrap_or(256);
        unsafe { write(1, &e.name[..len]) };
        unsafe { write(1, b"\n") };
    }

    0
}
```

---

## Tips

- **No heap by default** — implement a simple allocator on top of `syscall2(SYS_MMAP, ...)` if you need dynamic allocation
- **Strings must be null-terminated** — syscalls expect `&[u8]` paths ending in `\0`
- **Avoid panics** — panic requires a handler; prefer returning error codes
- **Use `core::` not `std::`** — no std available in no_std
- **Stack size is 2 MiB** — large stack arrays are fine, but avoid very deep recursion

---

## Syscall numbers for raw use

```rust
// Core I/O
pub const SYS_EXIT:    i64 = 1;
pub const SYS_OPEN:    i64 = 2;
pub const SYS_CLOSE:   i64 = 3;
pub const SYS_READ:    i64 = 4;
pub const SYS_WRITE:   i64 = 5;
pub const SYS_STAT:    i64 = 6;
pub const SYS_SEEK:    i64 = 7;
pub const SYS_MKDIR:   i64 = 8;
pub const SYS_UNLINK:  i64 = 9;
pub const SYS_READDIR: i64 = 10;
// Process
pub const SYS_SPAWN:   i64 = 11;
pub const SYS_WAIT:    i64 = 12;
pub const SYS_GETPID:  i64 = 13;
pub const SYS_YIELD:   i64 = 14;
// Memory
pub const SYS_MMAP:    i64 = 22;
pub const SYS_MUNMAP:  i64 = 23;
// Signals/IPC
pub const SYS_KILL:    i64 = 35;
pub const SYS_PIPE:    i64 = 38;
pub const SYS_DUP2:    i64 = 39;
// CWD
pub const SYS_CHDIR:   i64 = 33;
pub const SYS_GETCWD:  i64 = 34;
// Network
pub const SYS_NET_SEND: i64 = 30;
pub const SYS_NET_RECV: i64 = 31;
pub const SYS_NET_PING: i64 = 32;
```
