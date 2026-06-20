# Build System and Toolchain

## Prerequisites

| Tool | Minimum version | Purpose |
|------|----------------|---------|
| `clang` | 14.0 | C compiler (x86_64-apple-macosx13.0 target) |
| `ld64.lld` | bundled with LLVM | Mach-O linker |
| `lld` | bundled with LLVM | ELF linker (for kernel) |
| `mtools` | any | FAT32 image manipulation (mcopy, mdir, mformat) |
| `qemu-system-x86_64` | 7.0+ | Emulation |
| OVMF firmware | any | UEFI for QEMU |
| `rustup` + rust | stable | Building `hx` editor and `libasd_sys` |
| `cargo` | stable | Rust package manager |

### Install on Arch Linux
```sh
pacman -S clang lld mtools qemu-desktop edk2-ovmf rustup
rustup default stable
rustup target add x86_64-apple-darwin
```

### Install on MSYS2 (Windows)
```sh
pacman -S mingw-w64-ucrt-x86_64-clang \
          mingw-w64-ucrt-x86_64-lld \
          mingw-w64-ucrt-x86_64-mtools \
          mingw-w64-ucrt-x86_64-qemu \
          mingw-w64-ucrt-x86_64-edk2
```

---

## Repository layout

```
OpenASD/
├── boot/            asdboot UEFI bootloader
├── doc/             Design documents (internal)
├── documentation/   Developer documentation (this folder)
├── init/            PID 1 (asdinit), kernel shell, installer
├── kernel/          Kernel source
│   ├── arch/        Syscall, GDT, IDT, paging, Mach-O loader
│   ├── drv/         ADF, virtio-blk, virtio-net, PS/2 kbd
│   ├── ipc/         Ring buffers, ports
│   ├── mm/          Physical/virtual memory manager
│   ├── net/         UDP, ARP, ICMP
│   ├── sched/       CFS scheduler
│   ├── security/    Security module (permissions)
│   ├── usr/         User/group database
│   └── vfs/         VFS, ramfs, FFS, FAT32
├── userland/
│   ├── bin/         Userland utilities (C)
│   ├── fastfetch/   mifetch variant
│   ├── hx/          Helix-inspired editor (Rust)
│   ├── libasd/      C + Rust userland runtime library
│   ├── mifetch/     System info tool
│   └── sbin/        System daemons
└── Makefile         Top-level build
```

---

## Make targets

Run from the repository root:

| Target | Description |
|--------|-------------|
| `make all` | Build everything (boot, kernel, userland) |
| `make kernel` | Rebuild kernel only |
| `make userland` | Rebuild all userland binaries |
| `make prepare-run` | Build live image (`build/run/live.img`) |
| `make run` | Build + launch QEMU interactively |
| `make run-debug` | Launch with QEMU monitor on stdio |
| `make install-fresh` | Create a blank FAT32 install disk |
| `make bfd-hxtest` | Headless CI test (hxtest autotest) |
| `make clean` | Remove all build artifacts |

---

## Build outputs

| File | Description |
|------|-------------|
| `boot/build/BOOTX64.EFI` | asdboot bootloader (UEFI app) |
| `kernel/build/asdkernel.bin` | Kernel ELF (flat, loaded at 0x4000000) |
| `userland/bin/build/<name>` | Userland Mach-O binaries |
| `userland/hx/build/hx` | hx editor Mach-O binary |
| `build/run/live.img` | Live bootable FAT32 image (QEMU) |
| `build/disk/asd-target.img` | Install target disk image |

---

## Adding a new utility

1. Create `userland/bin/myutil.c`
2. Add `myutil` to `BINS` in `userland/bin/Makefile`
3. Add `"myutil"` to `LIVE_BINS` in the top-level `Makefile`
4. Add `"myutil"` to the `bins[]` array in `kernel/entry.c` (so it's seeded at boot)

```sh
make -C userland/bin    # rebuild just the binaries
make prepare-run        # rebuild live image
```

---

## Running in QEMU

```sh
make run
```

This launches:
```
qemu-system-x86_64
  -machine q35,accel=kvm   (or tcg if no KVM)
  -cpu host                 (or qemu64)
  -m 1024
  -drive live.img (bootindex=1)
  -drive asd-target.img (bootindex=2)
  -serial stdio             (serial output + keyboard)
```

QEMU serial output goes to your terminal. The kernel and all `serial_puts()` calls appear there.

### Without KVM (slower)
```sh
make QEMU_ACCEL=tcg run
```

---

## asdboot.conf

The bootloader reads `/boot/asdboot.conf` from the FAT32 volume:

```toml
timeout = 3

menu
  title = "OpenASD 1.0"
  default = "default"
end

entry
  id      = "default"
  label   = "OpenASD 1.0"
  kernel  = "/boot/asdkernel.bin"
  cmdline = ""
end
```

**cmdline options:**

| Option | Effect |
|--------|--------|
| (empty) | Normal boot, show login |
| `autotest_hxtest` | Run hxtest autotest and halt |
| `autotest_fastfetch` | Run fastfetch autotest and halt |

---

## Kernel build flags

The kernel is compiled as a freestanding ELF:

```sh
clang --target=x86_64-unknown-elf \
      -ffreestanding -fno-builtin \
      -fno-stack-protector -mno-red-zone \
      -nostdlib -Os
```

Linked with:
```sh
ld.lld -m elf_x86_64 -nostdlib -static \
       -Ttext=0x4000000 \
       --entry=kernel_main \
       -o asdkernel.bin *.o
```

The kernel is not an ELF at runtime — it's a flat binary loaded by asdboot directly into physical memory at `0x4000000`. The ELF header is read only to locate the entry point.

---

## CI / headless testing

Run the automated test suite without a display:

```sh
make bfd-hxtest
```

This:
1. Patches the disk with `cmdline = "autotest_hxtest"`
2. Runs QEMU headless for 25 seconds
3. Checks `build/debug/serial_hxtest.log` for `HXTEST OK`

Exit codes: 0 = pass, 1 = fail.

For custom autotests, see `init/asdinit.c` — add a `kernel_cmdline_has("autotest_X")` block and a corresponding binary.

---

## Disk image internals

The live image (`live.img`) is a flat FAT32 file (64 MiB):

```
live.img (FAT32)
├── EFI/
│   └── BOOT/
│       ├── BOOTX64.EFI    ← asdboot
│       ├── asdkernel.bin  ← kernel
│       └── asdboot.conf   ← boot config
├── boot/
│   ├── asdkernel.bin
│   └── asdboot.conf
└── bin/
    ├── asdsh
    ├── hx
    ├── ls
    ├── cat
    └── ... all LIVE_BINS
```

At boot, the kernel mounts `EFI/BOOT/` as `/boot` (FAT32, read-only) and copies all binaries from `/boot/bin/` into ramfs as `/bin/`.
