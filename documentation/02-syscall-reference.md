# Syscall Reference

All syscalls use the x86-64 `SYSCALL` instruction. Arguments are passed in registers: `rax` = syscall number, `rdi/rsi/rdx/r10/r8/r9` = arguments 0–5. Return value is in `rax`; negative values are errors (`-errno`).

The C interface (`__syscall()`) and all wrappers are in `userland/libasd/include/asd/syscall.h`.

---

## Core I/O

### SYS_EXIT (1)
```c
__attribute__((noreturn)) void asd_exit(int code);
```
Terminates the calling process with exit code `code`. Never returns.

---

### SYS_OPEN (2)
```c
int asd_open(const char *path, int flags);
```
Opens a file. Returns a file descriptor ≥ 3 on success, negative on error.

**flags** (OR-able):

| Flag | Value | Meaning |
|------|-------|---------|
| `O_RDONLY` | 0x01 | Open for reading |
| `O_WRONLY` | 0x02 | Open for writing |
| `O_RDWR` | 0x03 | Open for read+write |
| `O_CREAT` | 0x10 | Create if not exists |
| `O_TRUNC` | 0x20 | Truncate to zero on open |
| `O_APPEND` | 0x40 | Writes always go to end |
| `O_DIRECTORY` | 0x80 | Fail if not a directory |

Relative paths are resolved against the process's current working directory (set by `asd_chdir()`).

---

### SYS_CLOSE (3)
```c
int asd_close(int fd);
```
Closes a file descriptor. Returns 0 on success.

---

### SYS_READ (4)
```c
long asd_read(int fd, void *buf, size_t n);
```
Reads up to `n` bytes from `fd` into `buf`. Returns bytes actually read (0 = EOF), or negative on error.

- `fd == 0` with no stdin redirect: reads from PS/2 keyboard (blocking)
- `fd == 0` with pipe/file redirect: reads from the redirected source

---

### SYS_WRITE (5)
```c
long asd_write(int fd, const void *buf, size_t n);
```
Writes `n` bytes from `buf` to `fd`. Returns bytes written or negative on error.

- `fd == 1` or `fd == 2` with no redirect: outputs to framebuffer console and serial
- With pipe/file redirect: writes to the redirected destination

---

### SYS_STAT (6)
```c
int asd_stat(const char *path, asd_stat_t *st);
```
Fills `*st` with file metadata. Returns 0 or negative.

```c
typedef struct {
    int64_t  size;       /* file size in bytes; -1 if unknown */
    uint8_t  kind;       /* ASD_NODE_FILE=1, ASD_NODE_DIR=2 */
    uint64_t mtime_ns;   /* modification time (nanoseconds) */
    char     name[256];  /* base name */
    uint16_t uid;        /* owner user ID */
    uint16_t gid;        /* owner group ID */
    uint16_t mode;       /* permission bits */
} asd_stat_t;
```

---

### SYS_SEEK (7)
```c
long asd_seek(int fd, long offset, int whence);
```
Repositions the file cursor. Returns new absolute offset or negative on error.

| `whence` | Value | Meaning |
|----------|-------|---------|
| `SEEK_SET` | 0 | Absolute from beginning |
| `SEEK_CUR` | 1 | Relative to current position |
| `SEEK_END` | 2 | Relative to end of file |

---

### SYS_MKDIR (8)
```c
int asd_mkdir(const char *path);
```
Creates a directory. Returns 0 or negative.

---

### SYS_UNLINK (9)
```c
int asd_unlink(const char *path);
```
Deletes a file or empty directory. Returns 0 or negative.

---

### SYS_READDIR (10)
```c
int asd_readdir(const char *path, asd_dirent_t *buf, uint32_t cap, uint32_t *n_out);
```
Lists directory entries. `buf` must hold `cap` entries. On success, `*n_out` contains the number of entries filled.

```c
typedef struct {
    char    name[256];   /* entry name (null-terminated) */
    uint8_t kind;        /* ASD_NODE_FILE=1, ASD_NODE_DIR=2 */
    int64_t size;        /* size in bytes; -1 for directories */
} asd_dirent_t;
```

---

## Process management

### SYS_SPAWN (11)
```c
int asd_spawn(const char *path, const char **argv, const char **envp);
```
Loads and starts a Mach-O binary. Returns the child PID on success, 0 or negative on error.

- `argv` must be a NULL-terminated array of strings; `argv[0]` is the program name
- `envp` can be NULL
- The child inherits the parent's `fd_table`, `cwd`, `uid`, and `gid`

```c
const char *argv[] = { "/bin/ls", "/", NULL };
int pid = asd_spawn("/bin/ls", argv, NULL);
if (pid > 0) asd_wait(pid, NULL);
```

---

### SYS_WAIT (12)
```c
int asd_wait(int pid, asd_exit_info_t *info);
```
Blocks until child `pid` exits. If `info` is not NULL, filled with exit details:

```c
typedef struct {
    int32_t  exit_code;  /* exit code passed to asd_exit() */
    uint64_t cpu_ns;     /* CPU time consumed by child */
    uint64_t wall_ns;    /* wall time (currently 0) */
} asd_exit_info_t;
```

---

### SYS_GETPID (13)
```c
int asd_getpid(void);
```
Returns the calling process's PID.

---

### SYS_YIELD (14)
```c
void asd_yield(void);
```
Voluntary context switch. Gives the scheduler a chance to run other processes.

---

### SYS_TIME (15)
```c
uint64_t asd_time(void);
```
Returns a monotonic time value in units of 100 nanoseconds (10 MHz ticks). Use `asd_gettime_ns()` for nanoseconds.

---

## Identity

### SYS_GETUID (16) / SYS_GETGID (17) / SYS_GETEUID (18) / SYS_GETEGID (19)
```c
unsigned int asd_getuid(void);
unsigned int asd_getgid(void);
unsigned int asd_geteuid(void);
unsigned int asd_getegid(void);
```
Return real and effective user/group IDs.

| UID | Meaning |
|-----|---------|
| 0 | root |
| 1–999 | system accounts |
| 1000+ | regular users |

---

### SYS_GETPPID (20)
```c
int asd_getppid(void);
```
Returns the parent process's PID.

---

## System info

### SYS_UNAME (21)
```c
int asd_uname(asd_utsname_t *u);
```
Fills the uname structure:

```c
typedef struct {
    char sysname[65];    /* "OpenASD" */
    char nodename[65];   /* hostname */
    char release[65];    /* "1.0" */
    char version[65];    /* "#1" */
    char machine[65];    /* "x86_64" */
    char domainname[65]; /* "(none)" */
} asd_utsname_t;
```

---

## Memory management

### SYS_MMAP (22)
```c
void *asd_mmap(void *addr, size_t len, int prot, int flags, int fd, long off);
```
Allocates anonymous memory. Returns start address or `MAP_FAILED ((void*)-1)`.

**Supported flags:** `MAP_ANONYMOUS | MAP_PRIVATE` only. `fd` and `off` are ignored.

```c
void *buf = asd_mmap(NULL, 65536, PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
if (buf == MAP_FAILED) { /* error */ }
```

Allocation region: `0x500000000000` to `0x700000000000` (bump allocator, 512 GiB).

---

### SYS_MUNMAP (23)
```c
int asd_munmap(void *addr, size_t len);
```
Frees a region. Returns 0 or negative.

---

### SYS_BRK (24)
```c
void *asd_brk(void *new_brk);
```
Sets the program break (end of heap). Returns the new break address. Pass NULL to query current break.

---

## Terminal / misc

### SYS_IOCTL (25)
```c
int asd_ioctl(int fd, unsigned long req, void *arg);
```
Device control. Currently returns `ENOTTY` for all requests.

---

### SYS_WRITEV (26)
```c
long asd_writev(int fd, const asd_iovec_t *iov, int iovcnt);
```
Scatter-gather write. Equivalent to multiple `asd_write()` calls in sequence.

```c
typedef struct { void *iov_base; size_t iov_len; } asd_iovec_t;
```

---

### SYS_GETTIME_NS (27)
```c
uint64_t asd_gettime_ns(void);
```
Returns monotonic time in nanoseconds since boot.

---

### SYS_SETUID (28) / SYS_SETGID (29)
```c
int asd_setuid(unsigned int uid);
int asd_setgid(unsigned int gid);
```
Set effective UID/GID. Root can set any ID. Non-root can only set to their own ID, **except** wheel-group users can call `asd_setuid(0)` to gain root privileges (used by `do`).

---

## Network

### SYS_NET_SEND (30)
```c
int asd_net_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                 const void *buf, uint16_t len);
```
Sends a UDP datagram. `dst_ip` is host byte order. Returns bytes sent or negative.

---

### SYS_NET_RECV (31)
```c
int asd_net_recv(void *buf, uint16_t buf_sz,
                 uint32_t *src_ip, uint16_t *src_port);
```
Receives a UDP datagram. Non-blocking; returns -1 if no data. Fills `*src_ip` and `*src_port`.

---

### SYS_NET_PING (32)
```c
int asd_ping(uint32_t dst_ip, uint32_t *rtt_ms);
```
Sends an ICMP echo request. Blocks until reply or timeout. Returns 0 on reply (fills `*rtt_ms`), -1 on timeout. `dst_ip` in host byte order.

---

## Working directory

### SYS_CHDIR (33)
```c
int asd_chdir(const char *path);
```
Changes the calling process's current working directory. Children spawned after this call inherit the new CWD. Returns 0 or negative.

---

### SYS_GETCWD (34)
```c
int asd_getcwd(char *buf, size_t size);
```
Copies the current working directory path into `buf`. Returns the length of the path string, or negative on error.

---

## Signals and IPC

### SYS_KILL (35)
```c
int asd_kill(int pid, int sig);
```
Sends signal `sig` to process `pid`. Returns 0 or negative.

| Signal | Number | Default action |
|--------|--------|----------------|
| `SIGTERM` | 1 | Terminate gracefully |
| `SIGKILL` | 9 | Terminate immediately |
| `SIGCHLD` | 17 | Sent to parent on child exit (auto-cleared) |

Signals are delivered at the next syscall return boundary. `SIGKILL` cannot be blocked.

---

### SYS_PIPE (38)
```c
int asd_pipe(int fds[2]);
```
Creates an anonymous pipe with a 4096-byte ring buffer. `fds[0]` is the read end, `fds[1]` is the write end. Returns 0 or negative.

```c
int fds[2];
asd_pipe(fds);
int pid = asd_spawn(...);   /* child inherits both ends */
asd_close(fds[1]);          /* close write end in parent */
char buf[256];
long n = asd_read(fds[0], buf, sizeof(buf));
asd_close(fds[0]);
asd_wait(pid, NULL);
```

---

### SYS_DUP2 (39)
```c
int asd_dup2(int oldfd, int newfd);
```
Duplicates `oldfd` into `newfd`. If `newfd` is already open, it is closed first. Works for `newfd = 0/1/2` (stdin/stdout/stderr redirect). Returns `newfd` on success.

```c
/* Redirect stdout to a file */
int fd = asd_open("/output.txt", O_WRONLY | O_CREAT | O_TRUNC);
asd_dup2(fd, 1);            /* fd 1 now goes to the file */
int child = asd_spawn("/bin/ls", argv, NULL);
asd_close(fd);              /* child inherited it */
asd_dup2(saved, 1);         /* restore stdout */
asd_wait(child, NULL);
```
