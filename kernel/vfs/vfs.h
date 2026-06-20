/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 */

#ifndef ASD_VFS_H
#define ASD_VFS_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------------- */

typedef uint32_t fd_t;

/* ------------------------------------------------------------------------- */

#ifndef MAX_FDS
#define MAX_FDS         256
#endif
#define MAX_VFSNODES    1024
#define MAX_BACKENDS    16
#define VFS_PATH_MAX    256

/* Open flags — MUST match kernel/arch/syscall.h and userland libasd/include/asd/syscall.h */
#define VFS_O_READ      0x01   /* O_RDONLY  */
#define VFS_O_WRITE     0x02   /* O_WRONLY  */
#define VFS_O_CREAT     0x10   /* O_CREAT   — was 0x04, now matches userland */
#define VFS_O_TRUNC     0x20   /* O_TRUNC   — was 0x08, now matches userland */
#define VFS_O_APPEND    0x40   /* O_APPEND  */
#define VFS_O_DIR       0x80   /* O_DIRECTORY — was 0x10, now matches userland */

/* Seek whence (asd_seek) */
#define VFS_SEEK_SET    0
#define VFS_SEEK_CUR    1
#define VFS_SEEK_END    2

/* Node kinds */
#define VFS_NODE_FILE   1
#define VFS_NODE_DIR    2
#define VFS_NODE_RING   3
#define VFS_NODE_PORT   4
#define VFS_NODE_SHM    5

/* Error codes (negative return values) */
#define VFS_ERR         (-1)  /* generic error */
#define VFS_ENOENT      (-2)  /* path not found */
#define VFS_EINVAL      (-3)  /* invalid argument */
#define VFS_EPERM       (-4)  /* permission denied */
#define VFS_ENOSPC      (-5)  /* no space left */
#define VFS_ENODEV      (-6)  /* no such backend */
#define VFS_EISDIR      (-7)  /* is a directory */
#define VFS_ENOTDIR     (-8)  /* not a directory */
#define VFS_EMFILE      (-9)  /* too many open files */

/* ------------------------------------------------------------------------- */

typedef struct {
    int64_t  size;        /* file size in bytes, -1 if unknown */
    uint8_t  kind;        /* VFS_NODE_FILE, VFS_NODE_DIR, etc. */
    uint64_t mtime_ns;    /* last modification time */
    char     name[VFS_PATH_MAX];  /* base name */
    uint16_t uid;         /* owner user ID (0 = root)           */
    uint16_t gid;         /* owner group ID (0 = root)          */
    uint16_t mode;        /* permission bits (USR_MODE_* in usr/usr.h) */
    uint8_t  _pad[2];
} stat_info_t;

/* ------------------------------------------------------------------------- */

#define VFS_DIRENT_NAME_LEN  256

typedef struct {
    char     name[VFS_DIRENT_NAME_LEN];
    uint8_t  kind;       /* VFS_NODE_FILE or VFS_NODE_DIR */
    int64_t  size;
} vfs_dirent_t;

/* ------------------------------------------------------------------------- */

struct vfs_backend;
typedef struct vfs_backend vfs_backend_t;

/* Opaque backend context — backends define their own struct */
typedef void *vfs_bctx_t;

typedef struct {
    /* Open a file/dir at the given sub-path (relative to mount point). */
    vfs_bctx_t (*open)   (vfs_backend_t *be, const char *path, int flags);

    /* Close a backend context */
    void       (*close)  (vfs_backend_t *be, vfs_bctx_t bctx);

    /* Read up to n bytes into buf, advance cursor. */
    int        (*read)   (vfs_backend_t *be, vfs_bctx_t bctx,
                          void *buf, size_t n, size_t *got);

    /* Write n bytes from buf, advance cursor. */
    int        (*write)  (vfs_backend_t *be, vfs_bctx_t bctx,
                          const void *buf, size_t n);

    /* Seek to offset. whence: VFS_SEEK_SET/CUR/END. */
    int64_t    (*seek)   (vfs_backend_t *be, vfs_bctx_t bctx,
                          int64_t off, int whence);

    /* Read directory entries into buf (array of vfs_dirent_t). */
    int        (*readdir)(vfs_backend_t *be, vfs_bctx_t bctx,
                          vfs_dirent_t *buf, uint32_t cap, uint32_t *n_out);

    /* Get file metadata without opening. */
    int        (*stat)   (vfs_backend_t *be, const char *path, stat_info_t *out);

    /* Delete a file or empty directory. */
    int        (*unlink) (vfs_backend_t *be, const char *path);

    /* Create a directory. */
    int        (*mkdir)  (vfs_backend_t *be, const char *path);
} vfs_ops_t;

/* ------------------------------------------------------------------------- */

struct pcb;  /* forward */
typedef struct ringbuf_t ringbuf_t;  /* forward */

typedef struct vfsnode {
    uint32_t       node_id;     /* 0 = free slot */
    uint32_t       refcount;
    uint32_t       flags;       /* VFS_O_* flags from open */
    uint64_t       offset;      /* current read/write cursor */
    int64_t        size;        /* file size (-1 = unknown/stream) */
    char           path[VFS_PATH_MAX];
    vfs_backend_t *backend;     /* NULL for synthetic nodes */
    vfs_bctx_t     bctx;        /* backend-private context */
    struct pcb    *pcb;         /* owning process */
    ringbuf_t      *ring;       /* non-NULL for ring-buffer synthetic nodes */
    uint32_t       port_id;     /* non-0 for port synthetic nodes */
    struct vfsnode *next;       /* intrusive list */
} vfsnode_t;

/* ------------------------------------------------------------------------- */

#define VFS_BE_NAME_LEN  64

struct vfs_backend {
    char        name[VFS_BE_NAME_LEN];  /* backend identifier */
    char        mount_pt[VFS_PATH_MAX]; /* mount prefix (e.g. "/root") */
    vfs_ops_t   ops;                    /* function pointers */
    void       *ctx;                    /* backend-private state */
    uint32_t    id;                     /* 0 = free slot */
};

/* ------------------------------------------------------------------------- */

/* Initialise the VFS subsystem. Must be called before any file operations. */
void vfs_init(void);

/* Register a backend (mount a filesystem). Returns backend ID or 0 on error. */
uint32_t vfs_mount(const char *mount_pt, vfs_ops_t ops, void *ctx);

/* Open a file. Returns fd (≥1) on success, negative on error. */
fd_t vfs_open(const char *path, int flags, struct pcb *caller);

/* Close a file descriptor. Returns 0 on success, negative on error. */
int  vfs_close(fd_t fd);

/* Read bytes. Returns 0 on success, fills *got. */
int  vfs_read(fd_t fd, void *buf, size_t n, size_t *got);

/* Write bytes. Returns 0 on success. */
int  vfs_write(fd_t fd, const void *buf, size_t n);

/* Seek. Returns new offset (≥0) on success, negative on error. */
int64_t vfs_seek(fd_t fd, int64_t off, int whence);

/* Get file metadata. Returns 0 on success. */
int  vfs_stat(const char *path, stat_info_t *out);

/* Delete a file or empty directory. Returns 0 on success. */
int  vfs_unlink(const char *path);

/* Create a directory. Returns 0 on success. */
int  vfs_mkdir(const char *path);

/* Read directory entries. Returns 0 on success, fills *n_out. */
int  vfs_readdir(const char *path, vfs_dirent_t *buf,
                 uint32_t cap, uint32_t *n_out);

/* Resolve "." and ".." components in a path. */
int  vfs_canonicalize(const char *path, char *out, size_t out_sz);

/* Register a synthetic ring-buffer node at the given path. */
int  vfs_register_ring(const char *path, ringbuf_t *ring);

/* Unregister a synthetic node. */
int  vfs_unregister_node(const char *path);

/* Close all file descriptors held by a process (call on exit). */
void vfs_close_all(struct pcb *pcb);

/* Inherit parent's fd_table into child at spawn time. */
void vfs_fd_inherit(struct pcb *child, struct pcb *parent);

/* Create anonymous pipe; installs read-end and write-end in current process. */
int vfs_pipe(int *rfd_out, int *wfd_out);

/* Duplicate oldfd into newfd (handles 0/1/2 for stdio redirect). */
int vfs_dup2(fd_t oldfd, fd_t newfd);

#endif /* ASD_VFS_H */
