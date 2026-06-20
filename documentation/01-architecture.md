# OpenASD Architecture

## Overview

OpenASD is a monolithic-style kernel with a microkernel-inspired design: the kernel handles scheduling, memory, VFS, and drivers, while userland programs are isolated in separate address spaces and communicate via syscalls.

```
┌─────────────────────────────────────────────────────────┐
│                     User Space                          │
│  asdsh    hx    grep    ls    cat    your-program ...   │
│  ─────────────────────────────────────────────────────  │
│                  libasd (C) / libasd_sys (Rust)         │
└──────────────────────┬──────────────────────────────────┘
                 SYSCALL / SYSRET
┌──────────────────────┴──────────────────────────────────┐
│                     Kernel Space                        │
│                                                         │
│  ┌───────────┐  ┌─────────┐  ┌──────────┐  ┌───────┐  │
│  │ Scheduler │  │   VFS   │  │  Memory  │  │  IPC  │  │
│  │   (CFS)   │  │  Layer  │  │ Manager  │  │ Ports │  │
│  └───────────┘  └────┬────┘  └──────────┘  └───────┘  │
│                      │                                  │
│  ┌─────────┐  ┌──────┴────┐  ┌──────────┐  ┌───────┐  │
│  │  ramfs  │  │    FFS    │  │  FAT32   │  │  ADF  │  │
│  └─────────┘  └───────────┘  └──────────┘  └───────┘  │
│                                                         │
│  ┌──────────────────────────────────────────────────┐  │
│  │         Hardware Abstraction / Drivers           │  │
│  │   virtio-blk  virtio-net  PS/2 kbd  framebuffer  │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
                        │ Hardware
┌─────────────────────────────────────────────────────────┐
│      x86-64  UEFI firmware  virtio PCI  PS/2  VGA      │
└─────────────────────────────────────────────────────────┘
```

---

## Boot sequence

```
Power on
  → UEFI firmware (OVMF)
  → asdboot (EFI/BOOT/BOOTX64.EFI)
      reads asdboot.conf from /boot/
      loads kernel image (asdkernel.bin) into memory
      builds Boot Info Block (BIB)
      exits UEFI boot services
  → Kernel (asdkernel.bin, loaded at 0x4000000)
      sets up GDT, IDT, PIC, PIT
      initialises scheduler, VFS, drivers
      mounts root filesystem
      seeds /bin/ from /boot/bin/
      launches asdinit (PID 1)
  → asdinit (PID 1)
      shows boot menu or auto-login
      launches asdsh for interactive use
  → asdsh
      user enters commands
```

---

## Address space layout

### Kernel

| Region | Address | Description |
|--------|---------|-------------|
| Kernel image | 0x0000000004000000 | Loaded by asdboot |
| Physical map | 0xFFFFC80000000000 | All physical RAM mapped here |
| Heap (kmalloc) | dynamic | Buddy allocator |

### User process

| Region | Address | Description |
|--------|---------|-------------|
| `__PAGEZERO` | 0x0–0x100000000 | Unmapped (null deref guard) |
| Mach-O segments | 0x100000000+ | `.text`, `.data`, `.bss` |
| mmap region | 0x500000000000+ | `asd_mmap()` allocations |
| User stack | 0x7FFE00000000–0x7FFF00000000 | 2 MiB, grows downward |

---

## Kernel subsystems

### Scheduler

CFS-style priority scheduler with red-black run queues.

- **Scheduling classes:** `SCHED_RT` (real-time), `SCHED_INTR` (interrupt), `SCHED_BATCH` (normal)
- **Preemption:** timer-driven via PIT at 100 Hz
- **PCB fields of interest:** `pid`, `ppid`, `uid`, `gid`, `cwd`, `fd_table`, `sig_pending`

### VFS

A layered VFS supporting multiple backend filesystems mounted at different paths.

- **Longest-prefix mount point matching** (e.g. `/root` wins over `/`)
- **Anonymous pipes** via ringbuf (no path, created with `vfs_pipe()`)
- **fd inheritance** at spawn (child gets copy of parent's fd_table)

### Memory manager

- **Physical:** buddy allocator (18 orders, page-granular)
- **Virtual:** per-process PML4 page tables
- **Kernel heap:** `kmalloc()` / `kfree()`
- **User heap:** `asd_mmap(MAP_ANONYMOUS|MAP_PRIVATE)` or `asd_brk()`

### Driver framework (ADF)

Loadable kernel drivers identified by an `adf_meta_t` header. Loaded from disk, probed by bus type, communicate with userland via shared ring buffers and IPC ports.

---

## Process lifecycle

```
spawn(path, argv, envp)
  → vfs_open(path)          read binary from VFS
  → macho_load()            map segments into new address space
  → build_stack_macho()     push argc/argv/envp onto user stack
  → sched_spawn()           allocate PCB, inherit cwd + fd_table
  → [child runs]
  → sched_exit(code)        mark PROC_DEAD, send SIGCHLD to parent
wait(pid, &info)
  → sched_reap(pid, &info)  collect exit status, free PCB
```

---

## File descriptors

| fd | Default | Override |
|----|---------|---------|
| 0 | keyboard (stdin) | `asd_dup2(pipe_rd, 0)` |
| 1 | framebuffer + serial (stdout) | `asd_dup2(pipe_wr, 1)` |
| 2 | same as stdout (stderr) | `asd_dup2(file, 2)` |
| 3–255 | files / pipes | `asd_open()` / `asd_pipe()` |

When a process spawns a child, the child inherits its entire `fd_table`. Use this to set up pipes before spawning.
