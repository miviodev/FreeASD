# IPC, Pipes, and Ring Buffers

## Overview

OpenASD provides three IPC mechanisms:

| Mechanism | Use case | API |
|-----------|----------|-----|
| Anonymous pipes | Shell pipelines, parent↔child | `asd_pipe()`, `asd_dup2()` |
| Ring buffers | Kernel↔driver, driver↔userland | `ringbuf_*()` |
| Ports | Named message queues | `asd_port_*()` (kernel only for now) |

---

## Anonymous pipes

A pipe is a unidirectional byte stream with a 4096-byte kernel buffer.

### Creating a pipe

```c
int fds[2];
if (asd_pipe(fds) != 0) {
    /* error */
}
/* fds[0] = read end
 * fds[1] = write end */
```

### Typical producer/consumer pattern

```c
int fds[2];
asd_pipe(fds);

/* === Parent becomes consumer, child is producer === */

/* Save parent's stdin, redirect from pipe read-end */
int saved_in = 10;
asd_dup2(0, saved_in);    /* save original stdin */
asd_dup2(fds[0], 0);      /* child will produce here */

const char *argv[] = { "/bin/ls", "/", NULL };
int child = asd_spawn("/bin/ls", argv, NULL);

asd_dup2(saved_in, 0);    /* restore our stdin */
asd_close(saved_in);
asd_close(fds[0]);        /* close our copy of read-end */

/* write-end (fds[1]) was inherited by child as fd 1 (stdout) */
/* child writes its output there */

asd_wait(child, NULL);
asd_close(fds[1]);
```

### Shell-style pipe: cmd1 | cmd2

```c
int fds[2];
asd_pipe(fds);

/* cmd1 → pipe write-end */
int saved1 = 11;
asd_dup2(1, saved1);
asd_dup2(fds[1], 1);       /* redirect cmd1 stdout to pipe */
int pid1 = asd_spawn("/bin/cmd1", argv1, NULL);
asd_dup2(saved1, 1);       /* restore */
asd_close(saved1);
asd_close(fds[1]);

/* cmd2 ← pipe read-end */
int saved0 = 12;
asd_dup2(0, saved0);
asd_dup2(fds[0], 0);       /* redirect cmd2 stdin from pipe */
int pid2 = asd_spawn("/bin/cmd2", argv2, NULL);
asd_dup2(saved0, 0);
asd_close(saved0);
asd_close(fds[0]);

asd_wait(pid1, NULL);
asd_wait(pid2, NULL);
```

### Pipe buffer size

The kernel pipe buffer is **4096 bytes**. If the producer fills the buffer before the consumer reads, writes return with partial counts or errors. Design pipelines to consume data promptly.

### Pipe close semantics

- When the write-end is closed and the read-end is read again → `asd_read()` returns 0 (EOF)
- Close both ends when done; inherited copies are closed automatically when the process exits

---

## File redirection

Use `asd_dup2()` to redirect stdin or stdout to files:

### Output redirection (`>`)

```c
int fd = asd_open("/output.txt", O_WRONLY | O_CREAT | O_TRUNC);
int saved = 13;
asd_dup2(1, saved);    /* save stdout */
asd_dup2(fd, 1);       /* redirect stdout to file */
asd_close(fd);

int pid = asd_spawn("/bin/ls", argv, NULL);

asd_dup2(saved, 1);    /* restore stdout */
asd_close(saved);
asd_wait(pid, NULL);
```

### Append redirection (`>>`)

```c
int fd = asd_open("/log.txt", O_WRONLY | O_CREAT | O_APPEND);
/* ... same pattern as above ... */
```

### Input redirection (`<`)

```c
int fd = asd_open("/input.txt", O_RDONLY);
int saved = 14;
asd_dup2(0, saved);
asd_dup2(fd, 0);
asd_close(fd);

int pid = asd_spawn("/bin/cat", argv, NULL);

asd_dup2(saved, 0);
asd_close(saved);
asd_wait(pid, NULL);
```

---

## Ring buffers (kernel-side)

Ring buffers are used internally for:
- **ADF drivers:** RX/TX queues between kernel driver and hardware
- **Kernel pipes:** backing store for `asd_pipe()`
- **IPC ports:** message queues between kernel components

### API (from `kernel/ipc/ringbuf.h`)

```c
/* Calculate how many bytes you need to allocate */
size_t ringbuf_required_size(uint32_t slot_size, uint32_t slot_count);

/* Initialize a ring buffer in pre-allocated memory */
int ringbuf_init(void *mem, uint32_t slot_size, uint32_t slot_count);

/* Push one slot (returns 0 on success, -1 if full or data too large) */
int ringbuf_push(ringbuf_t *rb, const void *data, uint32_t size);

/* Pop one slot (returns 0 on success, -1 if empty or buf too small) */
int ringbuf_pop(ringbuf_t *rb, void *buf, uint32_t size);

/* Batch operations */
uint32_t ringbuf_push_batch(ringbuf_t *rb, const void *items,
                             uint32_t item_size, uint32_t n);
uint32_t ringbuf_pop_batch(ringbuf_t *rb, void *buf,
                            uint32_t item_size, uint32_t n);

/* Status */
int      ringbuf_empty(const ringbuf_t *rb);
int      ringbuf_full(const ringbuf_t *rb);
uint32_t ringbuf_used(const ringbuf_t *rb);
uint32_t ringbuf_free(const ringbuf_t *rb);
```

### Constraints

- `slot_size` must be a power of 2 and ≥ 8
- `slot_count` must be a power of 2 and ≥ 2
- `ringbuf_push()` copies exactly `slot_size` bytes — pad smaller data if needed
- The structure is lock-free for single-producer / single-consumer use

### Example: allocating a ring buffer in a driver

```c
#include "ipc/ringbuf.h"

#define SLOT_SIZE  128    /* bytes per packet slot */
#define SLOT_COUNT 64     /* number of slots */

static ringbuf_t *g_rx_ring = NULL;

void my_driver_init(void) {
    size_t sz = ringbuf_required_size(SLOT_SIZE, SLOT_COUNT);
    g_rx_ring = (ringbuf_t *)kmalloc(sz);
    if (!g_rx_ring) return;

    if (ringbuf_init(g_rx_ring, SLOT_SIZE, SLOT_COUNT) != 0) {
        kfree(g_rx_ring);
        g_rx_ring = NULL;
        return;
    }

    serial_puts("ring buffer ready\n");
}

/* Called from hardware interrupt */
void my_driver_rx_irq(const uint8_t *pkt, size_t pkt_len) {
    uint8_t slot[SLOT_SIZE];
    size_t copy = pkt_len < SLOT_SIZE ? pkt_len : SLOT_SIZE;
    memcpy(slot, pkt, copy);

    if (ringbuf_push(g_rx_ring, slot, SLOT_SIZE) != 0) {
        serial_puts("rx ring full, packet dropped\n");
    }
}

/* Called from consumer (network stack or userland) */
int my_driver_read(uint8_t *out, size_t out_sz) {
    uint8_t slot[SLOT_SIZE];
    if (ringbuf_pop(g_rx_ring, slot, SLOT_SIZE) != 0)
        return -1;   /* empty */
    size_t copy = out_sz < SLOT_SIZE ? out_sz : SLOT_SIZE;
    memcpy(out, slot, copy);
    return (int)copy;
}
```

---

## Ring buffer memory layout

```
Offset  Content
 0      magic (8 bytes): "RINGBUF1"
 8      version (4 bytes)
12      slot_size (4 bytes)
16      slot_count (4 bytes)
20      _pad (4 bytes)
24      write_idx (8 bytes, atomic)
32      read_idx  (8 bytes, atomic)
40      _reserved (24 bytes)
64      data[slot_count * slot_size]
```

Total size: `64 + slot_size * slot_count` bytes.

---

## Synthetic VFS nodes for pipes

When `vfs_pipe()` is called, the kernel allocates a ring buffer and two `vfsnode_t` entries:
- **read-end node:** `flags = VFS_O_READ`, `ring = rb`
- **write-end node:** `flags = VFS_O_WRITE`, `ring = rb`

`vfs_read()` and `vfs_write()` detect `node->ring != NULL` and call `ringbuf_pop()` / `ringbuf_push()` respectively. This means pipes behave exactly like files from userland's perspective.

---

## IPC ports (kernel-only, v1)

Ports are named FIFO message channels. In v1 they are primarily used by ADF drivers:

```c
/* Open or create a named port */
port_t port = asd_port_open("my.driver.ctrl", 0);

/* Send a message */
uint8_t msg[32] = { /* ... */ };
asd_port_send(port, msg, sizeof(msg));

/* Receive a message */
uint8_t buf[32];
size_t got = 0;
asd_port_recv(port, buf, sizeof(buf), &got);

/* Close */
asd_port_close(port);
```

Userland port access via syscalls is planned for v2.
