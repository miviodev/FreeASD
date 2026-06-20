# Debugging

## Serial output

All kernel and init output goes to COM1 (serial port). QEMU connects it to your terminal via `-serial stdio` (in `make run`) or to a file.

### Reading serial output

```sh
# Interactive run — serial appears in terminal
make run

# Headless run — serial saved to file
make bfd-hxtest
cat build/debug/serial_hxtest.log
```

### Adding serial output in kernel code

```c
#include "console/fbcon.h"

serial_puts("debug message\n");
serial_putu(some_uint64_value);
serial_putc('\n');
```

### Adding output in userland code

```c
#include <asd/syscall.h>

/* fd 1 goes to both framebuffer AND serial */
asd_write(1, "debug: reached point A\n", 23);
```

---

## QEMU monitor

```sh
make run-debug
```

This adds `-monitor stdio` so you can type QEMU commands while the OS runs:

```
(qemu) info registers       # dump CPU registers
(qemu) info mem             # dump page tables
(qemu) x /10i $pc           # disassemble 10 instructions at PC
(qemu) p $rax               # print register value
(qemu) stop                 # pause execution
(qemu) cont                 # resume
(qemu) quit                 # exit QEMU
```

---

## Kernel crash output

When the kernel hits an unhandled exception it prints to serial:

```
*** EXCEPTION #PF err=0x0002 cr2=0x00000000DEADBEEF rip=0x00000004001234 rsp=0x00000004100000
```

| Field | Meaning |
|-------|---------|
| `#PF` | Exception name (Page Fault, GP fault, etc.) |
| `err` | Error code (for #PF: bit0=protection, bit1=write, bit2=user) |
| `cr2` | Faulting address (for #PF) |
| `rip` | Instruction pointer at fault |
| `rsp` | Stack pointer at fault |

### Exception names

| Name | Description |
|------|-------------|
| `#DE` | Divide by zero |
| `#BR` | Bounds check |
| `#UD` | Invalid opcode |
| `#NM` | Device not available |
| `#DF` | Double fault |
| `#TS` | Invalid TSS |
| `#NP` | Segment not present |
| `#SS` | Stack fault |
| `#GP` | General protection fault |
| `#PF` | Page fault |

---

## User process crash output

When a user process crashes, the kernel prints:

```
[user] #GP rip=0x00000001000005B0 rsp=0x00007FFEFFFFFE58
```

`rip` is the virtual address of the faulting instruction inside the process. To map it to a source location:

```sh
# Get the Mach-O binary offset (subtract text base 0x100000000)
python3 -c "print(hex(0x1000005B0 - 0x100000000))"
# → 0x5b0

# Disassemble around that offset
llvm-objdump -d userland/bin/build/myprogram | grep -A5 "5b0:"
```

Common causes of `[user] #GP`:
- **MOVAPS on unaligned address** — forgot `-mno-sse` compile flag
- **NULL pointer dereference** — `__PAGEZERO` (0–0x100000000) is unmapped
- **Stack overflow** — recursion depth > ~64K frames with typical frame sizes

---

## Autotest framework

The kernel supports headless CI tests via the `autotest_X` kernel cmdline:

### Adding a new autotest

1. Write a binary `userland/bin/mytest.c` that prints `MYTEST OK` on success
2. Add it to `BINS` and `LIVE_BINS`
3. Add to `kernel/entry.c` seed list
4. Add to `init/asdinit.c`:
   ```c
   if (kernel_cmdline_has("autotest_mytest")) {
       g_shell_uid = UID_ROOT;
       g_shell_gid = GID_ROOT;
       serial_puts("[autotest] running mytest\n");
       shell_autotest_exec("mytest");
       serial_puts("[autotest] done\n");
       for (;;) __asm__ volatile("cli; hlt");
   }
   ```
5. Add a Makefile target:
   ```makefile
   bfd-mytest: all
       # patch conf + run QEMU + grep for MYTEST OK
   ```

### Running the existing hxtest

```sh
make bfd-hxtest
# Prints: PASS: hxtest OK
```

---

## Tracing syscalls

To trace which syscalls a process makes, add logging to `syscall_dispatch()` in `kernel/arch/syscall.c`:

```c
uint64_t syscall_dispatch(uint64_t nr, ...) {
    /* Temporary: log all syscalls */
    serial_puts("syscall nr=");
    serial_putu(nr);
    serial_puts(" pid=");
    serial_putu(sched_current() ? sched_current()->pid : 0);
    serial_puts("\n");

    switch (nr) {
    /* ... */
    }
}
```

This produces verbose output on serial — filter with `grep` after the run.

---

## GDB stub (not yet in v1)

GDB remote debugging via QEMU's `-s -S` flags is planned but not wired up yet.

Manual workaround:
```sh
# In one terminal
make run-debug  # start QEMU

# In another terminal (after QEMU starts)
gdb userland/bin/build/myprogram
(gdb) target remote :1234
```

The kernel does not currently set up debug registers or handle GDB packets.

---

## Common problems

### QEMU boots to UEFI shell instead of OS

**Cause:** `OVMF_VARS.fd` has stale boot entries.

**Fix:**
```sh
# Reset OVMF variables
cp /usr/share/qemu/edk2-i386-vars.fd build/run/OVMF_VARS.fd
```

Or use the live image approach (which always works):
```sh
make prepare-run && make run
```

### Binary crashes with `[user] #GP` immediately

**Cause:** SSE `MOVAPS` instruction on unaligned stack address.

**Fix:** Ensure all compile flags include `-mno-sse -mno-sse2 -mno-avx`.

### `command not found` in asdsh

**Cause:** The binary is not in `/bin/` on the live image.

**Fix:**
```sh
# Check if it's in the image
mdir -i build/run/live.img ::/bin

# If not, add to LIVE_BINS in Makefile and rebuild
make prepare-run
```

### Serial output stops mid-boot

**Cause:** Kernel hang (usually a deadlock or infinite wait).

**Fix:** Use QEMU monitor (`make run-debug`) and type `info registers` to see where the CPU is stuck.

### Process hangs reading stdin

**Cause:** Reading from fd 0 without a terminal attached (e.g. in autotest mode).

**Fix:** In autotest binaries, don't read from stdin unless explicitly piping input. Or redirect stdin from `/dev/null` (not yet supported — use a pipe with an empty write-end instead).
