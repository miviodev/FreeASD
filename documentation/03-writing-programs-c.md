# Writing Programs in C

## Toolchain requirements

| Tool | Version | Purpose |
|------|---------|---------|
| `clang` | 14+ | C compiler targeting `x86_64-apple-macosx13.0` |
| `ld64.lld` | (bundled with LLVM) | Mach-O linker |
| `mtools` | any | Copy binaries into the disk image |

Install on Arch/MSYS2:
```sh
pacman -S clang lld mtools
```

---

## Project structure

```
my-program/
├── main.c
└── Makefile
```

The simplest possible `Makefile`:
```makefile
CC      = clang
LD      = ld64.lld
LIBASD  = $(ASD)/userland/libasd

CFLAGS  = --target=x86_64-apple-macosx13.0 \
          -nostdlib -nostdinc -ffreestanding \
          -fno-stack-protector -fno-builtin \
          -mno-red-zone -mno-sse -mno-sse2 -mno-avx \
          -I$(LIBASD)/include -Os

LDFLAGS = -arch x86_64 \
          -platform_version macos 13.0 13.0 \
          -e _start

my-program: main.o
	$(LD) $(LDFLAGS) $(LIBASD)/build/start.o $< $(LIBASD)/build/libasd.a -o $@

main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@
```

`$(ASD)` should point to the OpenASD source root.

**Critical compile flags:**

| Flag | Why |
|------|-----|
| `--target=x86_64-apple-macosx13.0` | Produce Mach-O 64-bit binary |
| `-nostdlib -nostdinc -ffreestanding` | No host libc — use libasd only |
| `-mno-red-zone` | Kernel does not preserve the 128-byte red zone |
| `-mno-sse -mno-sse2 -mno-avx` | SSE MOVAPS requires 16-byte alignment not guaranteed by kernel ABI |
| `-fno-stack-protector -fno-builtin` | No canaries, no compiler built-in functions |

---

## Minimal program

```c
#include <asd/syscall.h>

int main(int argc, const char **argv, const char **envp) {
    (void)envp;
    asd_write(1, "hello, world\n", 13);
    asd_exit(0);
}
```

The entry point `_start` (from `libasd/start.S`) extracts `argc`, `argv`, `envp` from the stack and calls `main()`. When `main` returns, `_start` calls `asd_exit()` with the return value.

---

## Standard output

There is no `printf` in libasd. Write output manually:

```c
#include <asd/syscall.h>

/* Write a null-terminated string to stdout */
static void puts(const char *s) {
    while (*s) { asd_write(1, s++, 1); }
}

/* Write a decimal number to stdout */
static void putn(long n) {
    if (n < 0) { asd_write(1, "-", 1); n = -n; }
    char buf[20]; int i = sizeof(buf);
    if (n == 0) { asd_write(1, "0", 1); return; }
    while (n > 0) { buf[--i] = '0' + (int)(n % 10); n /= 10; }
    asd_write(1, buf + i, sizeof(buf) - i);
}
```

Or use `libasd`'s `printf` if available:
```c
#include <asd/stdio.h>   /* if libasd includes stdio */
```

---

## File I/O

```c
#include <asd/syscall.h>

int main(void) {
    /* Write a file */
    int fd = asd_open("/myfile.txt", O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) { asd_exit(1); }
    asd_write(fd, "content\n", 8);
    asd_close(fd);

    /* Read it back */
    fd = asd_open("/myfile.txt", O_RDONLY);
    if (fd < 0) { asd_exit(1); }
    char buf[256];
    long n = asd_read(fd, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    asd_close(fd);

    asd_write(1, buf, (size_t)n);
    asd_exit(0);
}
```

---

## Directory listing

```c
#include <asd/syscall.h>

int main(void) {
    asd_dirent_t ents[64];
    uint32_t n = 0;

    if (asd_readdir("/", ents, 64, &n) != 0) {
        asd_write(2, "readdir failed\n", 15);
        asd_exit(1);
    }

    for (uint32_t i = 0; i < n; i++) {
        char kind = (ents[i].kind == ASD_NODE_DIR) ? 'd' : '-';
        asd_write(1, &kind, 1);
        asd_write(1, " ", 1);
        int nl = 0; while (ents[i].name[nl]) nl++;
        asd_write(1, ents[i].name, (size_t)nl);
        asd_write(1, "\n", 1);
    }
    asd_exit(0);
}
```

---

## Spawning child processes

```c
#include <asd/syscall.h>

int main(void) {
    const char *argv[] = { "/bin/ls", "/bin", NULL };
    int pid = asd_spawn("/bin/ls", argv, NULL);
    if (pid <= 0) {
        asd_write(2, "spawn failed\n", 13);
        asd_exit(1);
    }
    asd_exit_info_t info;
    asd_wait(pid, &info);
    /* info.exit_code contains child's exit code */
    asd_exit(info.exit_code);
}
```

---

## Pipes

```c
#include <asd/syscall.h>

int main(void) {
    int fds[2];
    asd_pipe(fds);   /* fds[0]=read, fds[1]=write */

    /* Redirect child's stdout to pipe write-end */
    int saved_stdout = 12;          /* temp fd for saving */
    asd_dup2(1, saved_stdout);       /* save current stdout */
    asd_dup2(fds[1], 1);            /* child will write here */

    const char *argv[] = { "/bin/ls", "/", NULL };
    int child = asd_spawn("/bin/ls", argv, NULL);

    asd_dup2(saved_stdout, 1);       /* restore our stdout */
    asd_close(saved_stdout);
    asd_close(fds[1]);               /* close write-end in parent */

    /* Read child's output */
    char buf[4096];
    long n;
    while ((n = asd_read(fds[0], buf, sizeof(buf))) > 0) {
        asd_write(1, buf, (size_t)n);
    }
    asd_close(fds[0]);
    asd_wait(child, NULL);
    asd_exit(0);
}
```

---

## Memory allocation

libasd provides a simple `malloc`/`free` backed by `asd_mmap`:

```c
#include <asd/alloc.h>    /* if provided */

/* Or use asd_mmap directly */
#include <asd/syscall.h>

void *my_alloc(size_t n) {
    return asd_mmap(NULL, n, PROT_READ | PROT_WRITE,
                    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
}
void my_free(void *p, size_t n) {
    asd_munmap(p, n);
}
```

---

## Signal handling

Signals are delivered at the next syscall boundary. There are no user-space signal handlers in v1 — signals terminate the process:

| Signal | Effect |
|--------|--------|
| `SIGKILL (9)` | Immediate termination |
| `SIGTERM (1)` | Graceful termination |
| `SIGCHLD (17)` | Auto-cleared; use `asd_wait()` instead |

To send a signal:
```c
asd_kill(other_pid, SIGTERM);   /* ask process to exit */
asd_kill(other_pid, SIGKILL);   /* force-kill */
```

---

## Installing your binary

After building, copy to the live image or install disk:

```sh
# Into live image
mcopy -o -i build/run/live.img my-program ::/bin/my-program

# Or rebuild the full image
cp my-program userland/bin/build/my-program
# Add "my-program" to LIVE_BINS in Makefile
make prepare-run
```

The init system seeds `/bin/` from `/boot/bin/` at boot, so binaries in the image under `/boot/bin/` become available as `/bin/<name>`.

---

## Common patterns

### Reading command-line arguments

```c
int main(int argc, const char **argv) {
    if (argc < 2) {
        asd_write(2, "usage: myprog <arg>\n", 20);
        asd_exit(1);
    }
    const char *arg = argv[1];
    /* use arg ... */
    asd_exit(0);
}
```

### Checking if running as root

```c
if (asd_getuid() != 0) {
    asd_write(2, "must be root\n", 13);
    asd_exit(1);
}
```

### Getting system uptime

```c
uint64_t ns = asd_gettime_ns();
uint64_t seconds = ns / 1000000000ULL;
```
