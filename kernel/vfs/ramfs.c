/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 */

#include "../vfs/vfs.h"
#include "../mm/mm.h"
#include <string.h>
#include <stddef.h>

typedef uint32_t spinlock_t;

static inline void spin_lock(spinlock_t *l) {
    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}

static inline void spin_unlock(spinlock_t *l) {
    __atomic_store_n(l, 0, __ATOMIC_RELEASE);
}

#define RAMFS_NAME_LEN  256
#define RAMFS_MAX_FILES 256

typedef struct {
    char     path[RAMFS_NAME_LEN];
    uint8_t *data;
    size_t   size;
    uint8_t  kind;    /* VFS_NODE_FILE or VFS_NODE_DIR */
    uint8_t  valid;
} ramfs_entry_t;

typedef struct {
    ramfs_entry_t entries[RAMFS_MAX_FILES];
    uint32_t      lock;
    const char   *name;   /* mount point name (e.g. "/boot") */
} ramfs_t;

typedef struct {
    ramfs_t      *fs;
    ramfs_entry_t *entry;
    size_t        offset;
} ramfs_file_t;

static ramfs_t g_boot_fs;      /* /boot */
static ramfs_t g_vfs_root_fs;  /* /  (writable root when no disk FAT at /) */
static ramfs_t g_root_fs;      /* /root */

static int ramfs_add_file(ramfs_t *fs, const char *path,
                          const void *data, size_t size);
int ramfs_add_dir(ramfs_t *fs, const char *path);

static ramfs_entry_t *ramfs_find(ramfs_t *fs, const char *path) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (fs->entries[i].valid &&
            strcmp(fs->entries[i].path, path) == 0)
            return &fs->entries[i];
    }
    return NULL;
}

static int ramfs_add_file(ramfs_t *fs, const char *path,
                          const void *data, size_t size)
{
    spin_lock(&fs->lock);
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!fs->entries[i].valid) {
            ramfs_entry_t *e = &fs->entries[i];
            strncpy(e->path, path, RAMFS_NAME_LEN);
            /* Copy data into kmalloc'd buffer */
            e->data = (uint8_t *)kmalloc(size);
            if (!e->data && size > 0) {
                spin_unlock(&fs->lock);
                return -1;
            }
            if (data && size > 0)
                memcpy(e->data, data, size);
            e->size = size;
            e->kind = VFS_NODE_FILE;
            e->valid = 1;
            spin_unlock(&fs->lock);
            return 0;
        }
    }
    spin_unlock(&fs->lock);
    return -1; /* full */
}

static vfs_bctx_t ramfs_open(vfs_backend_t *be, const char *path, int flags) {
    ramfs_t *fs = (ramfs_t *)be->ctx;
    if (!fs) return NULL;

    spin_lock(&fs->lock);
    ramfs_entry_t *e = ramfs_find(fs, path);

    /* Synthetic root directory handle */
    static ramfs_entry_t s_root = { .path = "/", .data = NULL, .size = 0,
                                    .kind = VFS_NODE_DIR, .valid = 1 };
    if (!e && (flags & VFS_O_DIR) && path[0] == '/' && path[1] == '\0')
        e = &s_root;

    if (!e && (flags & VFS_O_CREAT)) {
        /* Create a new empty file entry */
        for (int i = 0; i < RAMFS_MAX_FILES; i++) {
            if (!fs->entries[i].valid) {
                e = &fs->entries[i];
                strncpy(e->path, path, RAMFS_NAME_LEN);
                e->data  = NULL;
                e->size  = 0;
                e->kind  = VFS_NODE_FILE;
                e->valid = 1;
                break;
            }
        }
    }

    if (!e) {
        spin_unlock(&fs->lock);
        return NULL;
    }

    if (e->kind == VFS_NODE_DIR && !(flags & VFS_O_DIR)) {
        spin_unlock(&fs->lock);
        return NULL;
    }

    /* Truncate if requested */
    if ((flags & VFS_O_TRUNC) && e->kind == VFS_NODE_FILE && e->data) {
        kfree(e->data);
        e->data = NULL;
        e->size = 0;
    }

    ramfs_file_t *f = (ramfs_file_t *)kmalloc(sizeof(*f));
    if (!f) { spin_unlock(&fs->lock); return NULL; }
    f->fs     = fs;
    f->entry  = e;
    f->offset = 0;
    spin_unlock(&fs->lock);
    return (vfs_bctx_t)f;
}

static void ramfs_close(vfs_backend_t *be, vfs_bctx_t bctx) {
    (void)be;
    if (bctx) kfree(bctx);
}

static int ramfs_read(vfs_backend_t *be, vfs_bctx_t bctx,
                      void *buf, size_t n, size_t *got)
{
    (void)be;
    ramfs_file_t *f = (ramfs_file_t *)bctx;
    if (!f || !f->entry) return VFS_EINVAL;

    ramfs_entry_t *e = f->entry;
    if (f->offset >= e->size) {
        if (got) *got = 0;
        return 0; /* EOF */
    }

    size_t avail = e->size - f->offset;
    size_t to_read = (n < avail) ? n : avail;
    memcpy(buf, e->data + f->offset, to_read);
    f->offset += to_read;
    if (got) *got = to_read;
    return 0;
}

static int ramfs_write(vfs_backend_t *be, vfs_bctx_t bctx,
                       const void *buf, size_t n)
{
    (void)be;
    ramfs_file_t *f = (ramfs_file_t *)bctx;
    if (!f || !f->entry) return VFS_EINVAL;
    ramfs_t *fs = f->fs;
    ramfs_entry_t *e = f->entry;

    /* FIX (v5): integer overflow check on new_end before realloc */
    if (n > 0 && f->offset + n < f->offset) return VFS_ENOSPC;
    size_t new_end = f->offset + n;

    /* FIX (v5): hold the filesystem lock during the realloc to prevent
     * a TOCTOU race where two concurrent writers both see the old size,
     * both allocate a new buffer of the same size, and one writer's data
     * is silently lost when the other's kfree/kmalloc pair wins. */
    spin_lock(&fs->lock);
    if (new_end > e->size) {
        uint8_t *nb = (uint8_t *)kmalloc(new_end);
        if (!nb) { spin_unlock(&fs->lock); return VFS_ENOSPC; }
        if (e->data && e->size > 0) memcpy(nb, e->data, e->size);
        if (e->data) kfree(e->data);
        e->data = nb;
        e->size = new_end;
    }
    memcpy(e->data + f->offset, buf, n);
    f->offset += n;
    spin_unlock(&fs->lock);
    return 0;
}

static int64_t ramfs_seek(vfs_backend_t *be, vfs_bctx_t bctx,
                          int64_t off, int whence)
{
    (void)be;
    ramfs_file_t *f = (ramfs_file_t *)bctx;
    if (!f || !f->entry) return VFS_EINVAL;

    ramfs_entry_t *e = f->entry;
    int64_t new_off;

    switch (whence) {
    case VFS_SEEK_SET: new_off = off; break;
    case VFS_SEEK_CUR: new_off = (int64_t)f->offset + off; break;
    case VFS_SEEK_END: new_off = (int64_t)e->size + off; break;
    default: return VFS_EINVAL;
    }

    if (new_off < 0 || (uint64_t)new_off > e->size) return VFS_EINVAL;
    f->offset = (size_t)new_off;
    return new_off;
}

static int ramfs_readdir(vfs_backend_t *be, vfs_bctx_t bctx,
                         vfs_dirent_t *buf, uint32_t cap, uint32_t *n_out)
{
    (void)be;
    ramfs_file_t *f = (ramfs_file_t *)bctx;
    if (!f) return VFS_EINVAL;

    ramfs_t *fs = f->fs;
    uint32_t count = 0;

    spin_lock(&fs->lock);

    /* List entries whose path starts with the directory path */
    const char *dir = f->entry->path;
    size_t dlen = strlen(dir);

    for (int i = 0; i < RAMFS_MAX_FILES && count < cap; i++) {
        ramfs_entry_t *e = &fs->entries[i];
        if (!e->valid) continue;
        if (e == f->entry) continue; /* skip the dir itself */

        /* Extract the name component relative to dir */
        const char *name;
        if (dlen == 1 && dir[0] == '/') {
            /* Root dir: entries are "/name" — skip leading slash */
            if (e->path[0] != '/' || !e->path[1]) continue;
            name = e->path + 1;
        } else {
            if (strncmp(e->path, dir, dlen) != 0) continue;
            if (e->path[dlen] != '/') continue;
            name = e->path + dlen + 1;
        }
        const char *slash = strrchr(name, '/');
        /* If there's a slash deeper, this is a subdirectory entry */
        if (slash) {
            /* Check if we already added this subdirectory */
            size_t sub_len = (size_t)(slash - name);
            int found = 0;
            for (uint32_t j = 0; j < count; j++) {
                if (strncmp(buf[j].name, name, sub_len) == 0 &&
                    buf[j].name[sub_len] == '\0' &&
                    buf[j].kind == VFS_NODE_DIR) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                strncpy(buf[count].name, name, sub_len + 1);
                buf[count].kind = VFS_NODE_DIR;
                buf[count].size = 0;
                count++;
            }
        } else {
            strncpy(buf[count].name, name, VFS_DIRENT_NAME_LEN);
            buf[count].kind = e->kind;
            buf[count].size = (int64_t)e->size;
            count++;
        }
    }

    spin_unlock(&fs->lock);
    *n_out = count;
    return 0;
}

static void base_name_from_path(const char *path, char *out, size_t out_sz);

static int ramfs_stat(vfs_backend_t *be, const char *path, stat_info_t *out) {
    ramfs_t *fs = (ramfs_t *)be->ctx;
    if (!fs) return VFS_ENODEV;

    spin_lock(&fs->lock);
    ramfs_entry_t *e = ramfs_find(fs, path);
    if (!e) { spin_unlock(&fs->lock); return VFS_ENOENT; }

    out->size    = (int64_t)e->size;
    out->kind    = e->kind;
    out->mtime_ns = 0; /* ramfs has no timestamps */
    base_name_from_path(e->path, out->name, VFS_PATH_MAX);
    spin_unlock(&fs->lock);
    return 0;
}

static int ramfs_unlink(vfs_backend_t *be, const char *path) {
    (void)be; (void)path;
    return VFS_EPERM; /* read-only */
}

static int ramfs_mkdir(vfs_backend_t *be, const char *path) {
    ramfs_t *fs = (ramfs_t *)be->ctx;
    if (!fs) return VFS_ENODEV;
    return ramfs_add_dir(fs, path) == 0 ? 0 : VFS_ENOSPC;
}

static void base_name_from_path(const char *path, char *out, size_t out_sz) {
    const char *sep = strrchr(path, '/');
    const char *name = sep ? sep + 1 : path;
    strncpy(out, name, out_sz);
}

static const vfs_ops_t ramfs_ops = {
    .open    = ramfs_open,
    .close   = ramfs_close,
    .read    = ramfs_read,
    .write   = ramfs_write,
    .seek    = ramfs_seek,
    .readdir = ramfs_readdir,
    .stat    = ramfs_stat,
    .unlink  = ramfs_unlink,
    .mkdir   = ramfs_mkdir,
};

uint32_t ramfs_boot_init(const void *file_table, uint32_t count) {
    memset(&g_boot_fs, 0, sizeof(g_boot_fs));
    g_boot_fs.lock = 0;
    g_boot_fs.name = "/boot";

    if (file_table && count > 0) {
        /* file_table is an array of {path, data, size} tuples. */
    }

    return vfs_mount("/boot", ramfs_ops, &g_boot_fs);
}

uint32_t ramfs_root_init(const void *initrd_ptr, uint64_t initrd_sz) {
    memset(&g_root_fs, 0, sizeof(g_root_fs));
    g_root_fs.lock = 0;
    g_root_fs.name = "/root";

    if (initrd_ptr && initrd_sz > 0) {
        /* For initial bring-up: the initrd is a flat archive. */
        ramfs_add_file(&g_root_fs, "/initrd", initrd_ptr, (size_t)initrd_sz);
    }

    return vfs_mount("/root", ramfs_ops, &g_root_fs);
}

uint32_t ramfs_mount_root(void) {
    memset(&g_vfs_root_fs, 0, sizeof(g_vfs_root_fs));
    g_vfs_root_fs.lock = 0;
    g_vfs_root_fs.name = "/";
    return vfs_mount("/", ramfs_ops, &g_vfs_root_fs);
}

int ramfs_add_dir(ramfs_t *fs, const char *path) {
    spin_lock(&fs->lock);
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!fs->entries[i].valid) {
            ramfs_entry_t *e = &fs->entries[i];
            strncpy(e->path, path, RAMFS_NAME_LEN);
            e->data = NULL;
            e->size = 0;
            e->kind = VFS_NODE_DIR;
            e->valid = 1;
            spin_unlock(&fs->lock);
            return 0;
        }
    }
    spin_unlock(&fs->lock);
    return -1;
}

/* ---------------------------------------------------------------------- */
/* Public write interface for installer / init                             */
/* ---------------------------------------------------------------------- */

/* Create directory in /root; idempotent if already exists */
int ramfs_root_mkdir(const char *path) {
    if (ramfs_find(&g_vfs_root_fs, path)) return 0;
    return ramfs_add_dir(&g_vfs_root_fs, path);
}

/* Create or overwrite a file on the ramfs mounted at / */
int ramfs_root_writefile(const char *path, const void *data, size_t size) {
    spin_lock(&g_vfs_root_fs.lock);
    ramfs_entry_t *e = ramfs_find(&g_vfs_root_fs, path);
    if (e) {
        uint8_t *nb = (size > 0) ? (uint8_t *)kmalloc(size) : NULL;
        if (!nb && size > 0) { spin_unlock(&g_vfs_root_fs.lock); return -1; }
        if (e->data) kfree(e->data);
        e->data = nb;
        if (data && size > 0) memcpy(nb, data, size);
        e->size = size;
        spin_unlock(&g_vfs_root_fs.lock);
        return 0;
    }
    spin_unlock(&g_vfs_root_fs.lock);
    return ramfs_add_file(&g_vfs_root_fs, path, data, size);
}
