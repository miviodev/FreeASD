# Writing VFS Filesystem Backends

The OpenASD VFS uses a backend plugin model: any code that implements the `vfs_ops_t` interface can be registered as a filesystem and mounted at a path prefix.

---

## How the VFS works

```
vfs_open("/root/myfile.txt", O_RDONLY, pcb)
  │
  ├── canonicalize path → "/root/myfile.txt"
  ├── find_backend("/root/myfile.txt")
  │     → finds backend mounted at "/root"
  │     → rel_path = "/myfile.txt"
  │
  ├── be->ops.open(be, "/myfile.txt", O_RDONLY)
  │     → returns vfs_bctx_t (backend-specific context)
  │
  └── allocate vfsnode_t { backend=be, bctx=ctx }
        → install in caller->fd_table[i]
        → return fd=i
```

Mount points are matched by longest prefix. The backend with the longest matching prefix wins.

---

## The ops interface

```c
typedef struct {
    vfs_bctx_t (*open)   (vfs_backend_t *be, const char *path, int flags);
    void       (*close)  (vfs_backend_t *be, vfs_bctx_t bctx);
    int        (*read)   (vfs_backend_t *be, vfs_bctx_t bctx,
                          void *buf, size_t n, size_t *got);
    int        (*write)  (vfs_backend_t *be, vfs_bctx_t bctx,
                          const void *buf, size_t n);
    int64_t    (*seek)   (vfs_backend_t *be, vfs_bctx_t bctx,
                          int64_t off, int whence);
    int        (*readdir)(vfs_backend_t *be, vfs_bctx_t bctx,
                          vfs_dirent_t *buf, uint32_t cap, uint32_t *n_out);
    int        (*stat)   (vfs_backend_t *be, const char *path,
                          stat_info_t *out);
    int        (*unlink) (vfs_backend_t *be, const char *path);
    int        (*mkdir)  (vfs_backend_t *be, const char *path);
} vfs_ops_t;
```

Any operation you don't support: set the pointer to `NULL`. The VFS will return an appropriate error.

---

## Return values

All operations return:

| Value | Meaning |
|-------|---------|
| 0 | Success |
| `VFS_ERR (-1)` | Generic error |
| `VFS_ENOENT (-2)` | File or directory not found |
| `VFS_EINVAL (-3)` | Invalid argument |
| `VFS_EPERM (-4)` | Permission denied |
| `VFS_ENOSPC (-5)` | No space left on device |
| `VFS_ENODEV (-6)` | No such device/backend |
| `VFS_EISDIR (-7)` | Is a directory (not a file) |
| `VFS_ENOTDIR (-8)` | Not a directory |
| `VFS_EMFILE (-9)` | Too many open files |

---

## Backend context (`vfs_bctx_t`)

`vfs_bctx_t` is `void *` — it's whatever your backend needs per-open-file. Typically a pointer to a heap-allocated struct containing the file cursor, size, and a reference to the underlying data.

---

## Mounting

```c
#include "vfs/vfs.h"

extern vfs_ops_t my_fs_ops;  /* your ops table */
extern void *my_fs_ctx;      /* your filesystem state */

/* Mount at /myfs — all paths starting with /myfs/ go to your backend */
uint32_t backend_id = vfs_mount("/myfs", my_fs_ops, my_fs_ctx);
if (!backend_id) {
    serial_puts("mount failed\n");
}
```

---

## Complete example: a simple read-only ROM filesystem

This example implements a filesystem backed by a fixed in-memory array of files.

```c
#include "vfs/vfs.h"
#include "mm/mm.h"
#include <string.h>
#include <stddef.h>

/* ---- ROM filesystem data ---- */
typedef struct {
    const char *path;    /* absolute path relative to mount point */
    const char *data;
    size_t      size;
} rom_entry_t;

static const rom_entry_t g_rom[] = {
    { "/README",    "Welcome to ROM\n", 15 },
    { "/hello.txt", "Hello!\n",          7 },
    { NULL, NULL, 0 }
};

typedef struct {
    const rom_entry_t *entry;
    size_t             cursor;
} rom_ctx_t;

/* ---- Backend operations ---- */

static vfs_bctx_t rom_open(vfs_backend_t *be, const char *path, int flags) {
    (void)be; (void)flags;

    /* Find entry matching path */
    for (int i = 0; g_rom[i].path; i++) {
        if (strcmp(g_rom[i].path, path) == 0) {
            rom_ctx_t *ctx = kmalloc(sizeof(*ctx));
            if (!ctx) return NULL;
            ctx->entry  = &g_rom[i];
            ctx->cursor = 0;
            return (vfs_bctx_t)ctx;
        }
    }
    return NULL;  /* file not found → open returns VFS_ENOENT */
}

static void rom_close(vfs_backend_t *be, vfs_bctx_t bctx) {
    (void)be;
    kfree(bctx);
}

static int rom_read(vfs_backend_t *be, vfs_bctx_t bctx,
                    void *buf, size_t n, size_t *got) {
    (void)be;
    rom_ctx_t *ctx = bctx;
    size_t avail = ctx->entry->size - ctx->cursor;
    size_t take  = n < avail ? n : avail;
    if (take > 0) {
        memcpy(buf, ctx->entry->data + ctx->cursor, take);
        ctx->cursor += take;
    }
    *got = take;
    return 0;
}

static int64_t rom_seek(vfs_backend_t *be, vfs_bctx_t bctx,
                        int64_t off, int whence) {
    (void)be;
    rom_ctx_t *ctx = bctx;
    int64_t new_pos;
    switch (whence) {
        case 0: new_pos = off; break;                              /* SEEK_SET */
        case 1: new_pos = (int64_t)ctx->cursor + off; break;       /* SEEK_CUR */
        case 2: new_pos = (int64_t)ctx->entry->size + off; break;  /* SEEK_END */
        default: return VFS_EINVAL;
    }
    if (new_pos < 0) return VFS_EINVAL;
    ctx->cursor = (size_t)new_pos;
    return new_pos;
}

static int rom_stat(vfs_backend_t *be, const char *path, stat_info_t *out) {
    (void)be;

    /* Check if it's the root directory */
    if (path[0] == '/' && path[1] == '\0') {
        out->kind = VFS_NODE_DIR;
        out->size = -1;
        return 0;
    }

    for (int i = 0; g_rom[i].path; i++) {
        if (strcmp(g_rom[i].path, path) == 0) {
            out->kind = VFS_NODE_FILE;
            out->size = (int64_t)g_rom[i].size;
            /* extract base name */
            const char *name = strrchr(g_rom[i].path, '/');
            name = name ? name + 1 : g_rom[i].path;
            strncpy(out->name, name, sizeof(out->name) - 1);
            return 0;
        }
    }
    return VFS_ENOENT;
}

static int rom_readdir(vfs_backend_t *be, vfs_bctx_t bctx,
                       vfs_dirent_t *buf, uint32_t cap, uint32_t *n_out) {
    (void)be; (void)bctx;
    uint32_t count = 0;
    for (int i = 0; g_rom[i].path && count < cap; i++, count++) {
        const char *name = strrchr(g_rom[i].path, '/');
        name = name ? name + 1 : g_rom[i].path;
        strncpy(buf[count].name, name, VFS_DIRENT_NAME_LEN - 1);
        buf[count].kind = VFS_NODE_FILE;
        buf[count].size = (int64_t)g_rom[i].size;
    }
    *n_out = count;
    return 0;
}

/* ---- Ops table ---- */
static const vfs_ops_t g_rom_ops = {
    .open    = rom_open,
    .close   = rom_close,
    .read    = rom_read,
    .write   = NULL,      /* read-only: no write */
    .seek    = rom_seek,
    .readdir = rom_readdir,
    .stat    = rom_stat,
    .unlink  = NULL,      /* read-only */
    .mkdir   = NULL,      /* read-only */
};

/* ---- Registration ---- */
void romfs_init(void) {
    uint32_t id = vfs_mount("/rom", g_rom_ops, NULL);
    if (!id) serial_puts("romfs: mount failed\n");
    else      serial_puts("romfs: mounted at /rom\n");
}
```

---

## Integrating into the kernel

1. Add your source to `kernel/vfs/` (e.g. `myfs.c`)
2. Add it to `kernel/Makefile` build list
3. Call your `myfs_init()` from `kernel_main_body()` in `kernel/entry.c` after `vfs_init()`

---

## The `stat_info_t` structure

```c
typedef struct {
    int64_t  size;           /* bytes; -1 = unknown */
    uint8_t  kind;           /* VFS_NODE_FILE or VFS_NODE_DIR */
    uint64_t mtime_ns;       /* modification time in ns; 0 = unknown */
    char     name[256];      /* base filename (not full path) */
    uint16_t uid;            /* owner UID */
    uint16_t gid;            /* owner GID */
    uint16_t mode;           /* permission bits (not yet enforced) */
} stat_info_t;
```

---

## Tips

- The `path` argument to `open`, `stat`, `unlink`, `mkdir` is **relative to the mount point** — if you mount at `/rom` and the user opens `/rom/file.txt`, your backend receives `/file.txt`.
- `readdir` with `bctx=NULL` is called with a path; some backends use the path to locate the directory, some ignore it if they have a flat namespace.
- Return `NULL` from `open()` to signal "not found" — the VFS translates this to `VFS_ENOENT`.
- Path strings are always canonicalized (no `.` or `..`) before reaching the backend.
