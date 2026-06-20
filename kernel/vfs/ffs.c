/*
 * ASD FFS — UFS-inspired on-disk filesystem backed by a block_dev_t.
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Supports: format, mount, open/close/read/write, readdir, stat,
 *           mkdir, unlink.  Direct blocks + single-indirect per file
 *           (max ~4 MB per file).
 */

#include "ffs.h"
#include "vfs.h"
#include "../mm/mm.h"
#include <string.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Internal vol state (one static instance — we only mount one FFS)    */
/* ------------------------------------------------------------------ */

typedef struct {
    block_dev_t *dev;
    uint64_t     first_lba;   /* partition start in 512-byte sectors */
    uint32_t     spb;         /* sectors per FFS block (= FFS_BLOCK_SIZE/512) */
    ffs_superblock_t sb;      /* cached superblock */
    uint8_t     *imap;        /* in-memory inode bitmap */
    uint8_t     *bmap;        /* in-memory block bitmap */
    uint32_t     imap_bytes;
    uint32_t     bmap_bytes;
    int          dirty_sb;
} ffs_vol_t;

static ffs_vol_t g_ffs_vol;
static int       g_ffs_mounted;

/* ------------------------------------------------------------------ */
/* Low-level block I/O                                                  */
/* ------------------------------------------------------------------ */

static uint8_t g_io_buf[FFS_BLOCK_SIZE]; /* shared scratch — not reentrant */

static int ffs_read_blk(ffs_vol_t *v, uint32_t blk, void *buf) {
    uint64_t lba = v->first_lba + (uint64_t)blk * v->spb;
    return v->dev->read(v->dev, lba, v->spb, buf);
}

static int ffs_write_blk(ffs_vol_t *v, uint32_t blk, const void *buf) {
    uint64_t lba = v->first_lba + (uint64_t)blk * v->spb;
    return v->dev->write(v->dev, lba, v->spb, buf);
}

static int ffs_sb_flush(ffs_vol_t *v) {
    memset(g_io_buf, 0, FFS_BLOCK_SIZE);
    memcpy(g_io_buf, &v->sb, sizeof(v->sb));
    return ffs_write_blk(v, 1, g_io_buf);
}

/* ------------------------------------------------------------------ */
/* Bitmap helpers (operate on in-memory bitmaps, sync to disk)         */
/* ------------------------------------------------------------------ */

/*
 * Find first 0 bit in `bmap` (skipping index 0 which is always reserved),
 * set it, and return the index.  Returns 0 on failure.
 */
static uint32_t bmap_alloc(uint8_t *bmap, uint32_t nbytes) {
    /* Always mark index 0 as used so we can use 0 as "no allocation" */
    bmap[0] |= 0x01;
    for (uint32_t b = 0; b < nbytes; b++) {
        if (bmap[b] == 0xFF) continue;
        for (int i = 0; i < 8; i++) {
            uint32_t idx = b * 8 + (uint32_t)i;
            if (idx == 0) continue;
            if (!(bmap[b] & (1u << i))) {
                bmap[b] |= (1u << i);
                return idx;
            }
        }
    }
    return 0;
}

static void bmap_free(uint8_t *bmap, uint32_t idx) {
    bmap[idx / 8] &= ~(1u << (idx % 8));
}

static int bmap_test(const uint8_t *bmap, uint32_t idx) {
    return (bmap[idx / 8] >> (idx % 8)) & 1;
}

/* Flush inode bitmap blocks to disk */
static int ffs_imap_flush(ffs_vol_t *v) {
    for (uint32_t i = 0; i < v->sb.inode_bitmap_blocks; i++) {
        uint32_t off = i * FFS_BLOCK_SIZE;
        uint32_t len = v->imap_bytes - off;
        if (len > FFS_BLOCK_SIZE) len = FFS_BLOCK_SIZE;
        memset(g_io_buf, 0, FFS_BLOCK_SIZE);
        memcpy(g_io_buf, v->imap + off, len);
        if (ffs_write_blk(v, v->sb.inode_bitmap_start + i, g_io_buf) != 0)
            return -1;
    }
    return 0;
}

/* Flush block bitmap to disk */
static int ffs_bmap_flush(ffs_vol_t *v) {
    for (uint32_t i = 0; i < v->sb.block_bitmap_blocks; i++) {
        uint32_t off = i * FFS_BLOCK_SIZE;
        uint32_t len = v->bmap_bytes - off;
        if (len > FFS_BLOCK_SIZE) len = FFS_BLOCK_SIZE;
        memset(g_io_buf, 0, FFS_BLOCK_SIZE);
        memcpy(g_io_buf, v->bmap + off, len);
        if (ffs_write_blk(v, v->sb.block_bitmap_start + i, g_io_buf) != 0)
            return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Inode I/O                                                            */
/* ------------------------------------------------------------------ */

static int ffs_read_inode(ffs_vol_t *v, uint32_t ino, ffs_inode_t *out) {
    if (ino == 0 || ino >= v->sb.inode_count) return -1;
    uint32_t blk = v->sb.inode_table_start + ino / FFS_INODES_PER_BLK;
    uint32_t off = (ino % FFS_INODES_PER_BLK) * FFS_INODE_SIZE;
    static uint8_t ibuf[FFS_BLOCK_SIZE];
    if (ffs_read_blk(v, blk, ibuf) != 0) return -1;
    memcpy(out, ibuf + off, sizeof(*out));
    return 0;
}

static int ffs_write_inode(ffs_vol_t *v, uint32_t ino, const ffs_inode_t *in) {
    if (ino == 0 || ino >= v->sb.inode_count) return -1;
    uint32_t blk = v->sb.inode_table_start + ino / FFS_INODES_PER_BLK;
    uint32_t off = (ino % FFS_INODES_PER_BLK) * FFS_INODE_SIZE;
    static uint8_t ibuf[FFS_BLOCK_SIZE];
    if (ffs_read_blk(v, blk, ibuf) != 0) return -1;
    memcpy(ibuf + off, in, sizeof(*in));
    return ffs_write_blk(v, blk, ibuf);
}

/* ------------------------------------------------------------------ */
/* Block / inode allocation                                             */
/* ------------------------------------------------------------------ */

static uint32_t ffs_alloc_data_block(ffs_vol_t *v) {
    uint32_t idx = bmap_alloc(v->bmap, v->bmap_bytes);
    if (idx == 0) return 0;
    if (v->sb.free_blocks > 0) v->sb.free_blocks--;
    ffs_bmap_flush(v);
    ffs_sb_flush(v);
    /* Zero the block */
    memset(g_io_buf, 0, FFS_BLOCK_SIZE);
    ffs_write_blk(v, v->sb.data_start + idx, g_io_buf);
    return v->sb.data_start + idx;
}

static void ffs_free_data_block(ffs_vol_t *v, uint32_t blk) {
    if (blk < v->sb.data_start) return;
    uint32_t idx = blk - v->sb.data_start;
    if (idx >= v->sb.total_blocks) return;
    bmap_free(v->bmap, idx);
    v->sb.free_blocks++;
    ffs_bmap_flush(v);
    ffs_sb_flush(v);
}

static uint32_t ffs_alloc_inode(ffs_vol_t *v) {
    uint32_t ino = bmap_alloc(v->imap, v->imap_bytes);
    if (ino == 0) return 0;
    if (v->sb.free_inodes > 0) v->sb.free_inodes--;
    ffs_imap_flush(v);
    ffs_sb_flush(v);
    return ino;
}

static void ffs_free_inode(ffs_vol_t *v, uint32_t ino) {
    if (ino == 0 || ino >= v->sb.inode_count) return;
    bmap_free(v->imap, ino);
    v->sb.free_inodes++;
    ffs_imap_flush(v);
    ffs_sb_flush(v);
}

/* ------------------------------------------------------------------ */
/* Block addressing for a file (direct + single indirect)              */
/* ------------------------------------------------------------------ */

/* Get the disk block number for logical block index `idx` in inode.
 * If `alloc` is set, allocate missing blocks. Returns 0 on error. */
static uint32_t ffs_file_block(ffs_vol_t *v, ffs_inode_t *ino,
                                uint32_t idx, int alloc) {
    if (idx < FFS_DIRECT_BLOCKS) {
        if (ino->blocks[idx] == 0 && alloc) {
            uint32_t nb = ffs_alloc_data_block(v);
            if (!nb) return 0;
            ino->blocks[idx] = nb;
        }
        return ino->blocks[idx];
    }

    /* Single indirect */
    uint32_t iidx = idx - FFS_DIRECT_BLOCKS;
    if (iidx >= FFS_PTRS_PER_BLK) return 0; /* beyond indirect range */

    if (ino->indirect_block == 0) {
        if (!alloc) return 0;
        uint32_t ib = ffs_alloc_data_block(v);
        if (!ib) return 0;
        ino->indirect_block = ib;
    }

    static uint32_t ptrs[FFS_PTRS_PER_BLK];
    if (ffs_read_blk(v, ino->indirect_block, ptrs) != 0) return 0;

    if (ptrs[iidx] == 0 && alloc) {
        uint32_t nb = ffs_alloc_data_block(v);
        if (!nb) return 0;
        ptrs[iidx] = nb;
        ffs_write_blk(v, ino->indirect_block, ptrs);
    }
    return ptrs[iidx];
}

/* ------------------------------------------------------------------ */
/* Directory operations                                                 */
/* ------------------------------------------------------------------ */

/* Aligned entry length for a name of length `nlen` */
static uint16_t dirent_len(uint8_t nlen) {
    return (uint16_t)(((FFS_DIRENT_HDR + nlen) + 3u) & ~3u);
}

/*
 * Lookup name in directory inode `dir_ino`. Returns inode number or 0.
 */
static uint32_t ffs_dir_lookup(ffs_vol_t *v, ffs_inode_t *dir,
                                const char *name) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;
    uint64_t dir_size = dir->size;
    uint64_t pos = 0;
    static uint8_t dbuf[FFS_BLOCK_SIZE];

    while (pos < dir_size) {
        uint32_t blk_idx = (uint32_t)(pos / FFS_BLOCK_SIZE);
        uint32_t blk_off = (uint32_t)(pos % FFS_BLOCK_SIZE);
        uint32_t dblk = ffs_file_block(v, dir, blk_idx, 0);
        if (!dblk) break;
        if (ffs_read_blk(v, dblk, dbuf) != 0) break;

        while (blk_off + FFS_DIRENT_HDR <= FFS_BLOCK_SIZE) {
            ffs_dirent_t *de = (ffs_dirent_t *)(dbuf + blk_off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == nlen &&
                memcmp(de->name, name, nlen) == 0) {
                return de->inode;
            }
            blk_off += de->rec_len;
            pos     += de->rec_len;
        }
        /* If we didn't advance within the block, skip to next */
        if (blk_off == (uint32_t)(pos % FFS_BLOCK_SIZE))
            break;
    }
    return 0;
}

/*
 * Add a directory entry (name → ino) to directory inode `dir_ino`.
 * Modifies `dir` in memory; caller must write it back with ffs_write_inode.
 */
static int ffs_dir_add(ffs_vol_t *v, uint32_t dir_ino, ffs_inode_t *dir,
                       const char *name, uint32_t ino, uint8_t ftype) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;
    uint16_t need = dirent_len(nlen);
    static uint8_t dbuf[FFS_BLOCK_SIZE];

    /* Try to fit in existing block (find a free slot or expand last entry) */
    uint32_t nblks = (uint32_t)((dir->size + FFS_BLOCK_SIZE - 1) / FFS_BLOCK_SIZE);
    for (uint32_t bi = 0; bi < nblks; bi++) {
        uint32_t dblk = ffs_file_block(v, dir, bi, 0);
        if (!dblk) continue;
        if (ffs_read_blk(v, dblk, dbuf) != 0) continue;

        uint32_t off = 0;
        while (off + FFS_DIRENT_HDR <= FFS_BLOCK_SIZE) {
            ffs_dirent_t *de = (ffs_dirent_t *)(dbuf + off);
            if (de->rec_len == 0) {
                /* Rest of block is free — write here */
                uint16_t avail = (uint16_t)(FFS_BLOCK_SIZE - off);
                if (avail < need) break;
                de->inode    = ino;
                de->rec_len  = avail;
                de->name_len = nlen;
                de->file_type = ftype;
                memcpy(de->name, name, nlen);
                ffs_write_blk(v, dblk, dbuf);
                return 0;
            }
            /* Deleted entry with enough space? */
            if (de->inode == 0 && de->rec_len >= need) {
                de->inode    = ino;
                de->name_len = nlen;
                de->file_type = ftype;
                memcpy(de->name, name, nlen);
                ffs_write_blk(v, dblk, dbuf);
                return 0;
            }
            /* Active entry — can we split off trailing space? */
            if (de->inode != 0) {
                uint16_t actual = dirent_len(de->name_len);
                uint16_t spare  = (uint16_t)(de->rec_len - actual);
                if (spare >= need) {
                    /* Shrink current entry, place new after it */
                    de->rec_len = actual;
                    ffs_dirent_t *ne = (ffs_dirent_t *)(dbuf + off + actual);
                    ne->inode     = ino;
                    ne->rec_len   = spare;
                    ne->name_len  = nlen;
                    ne->file_type = ftype;
                    memcpy(ne->name, name, nlen);
                    ffs_write_blk(v, dblk, dbuf);
                    return 0;
                }
            }
            off += de->rec_len;
        }
    }

    /* Allocate a new block for this directory */
    uint32_t new_blk = ffs_file_block(v, dir, nblks, 1);
    if (!new_blk) return -1;
    memset(dbuf, 0, FFS_BLOCK_SIZE);
    ffs_dirent_t *de = (ffs_dirent_t *)dbuf;
    de->inode     = ino;
    de->rec_len   = (uint16_t)FFS_BLOCK_SIZE;
    de->name_len  = nlen;
    de->file_type = ftype;
    memcpy(de->name, name, nlen);
    if (ffs_write_blk(v, new_blk, dbuf) != 0) return -1;
    dir->size += FFS_BLOCK_SIZE;
    return 0;
}

/*
 * Remove entry named `name` from directory inode.
 * Returns 0 on success, -1 if not found.
 */
static int ffs_dir_remove(ffs_vol_t *v, ffs_inode_t *dir, const char *name) {
    uint8_t nlen = 0;
    while (name[nlen]) nlen++;
    static uint8_t dbuf[FFS_BLOCK_SIZE];
    uint32_t nblks = (uint32_t)((dir->size + FFS_BLOCK_SIZE - 1) / FFS_BLOCK_SIZE);

    for (uint32_t bi = 0; bi < nblks; bi++) {
        uint32_t dblk = ffs_file_block(v, dir, bi, 0);
        if (!dblk) continue;
        if (ffs_read_blk(v, dblk, dbuf) != 0) continue;
        uint32_t off = 0;
        while (off + FFS_DIRENT_HDR <= FFS_BLOCK_SIZE) {
            ffs_dirent_t *de = (ffs_dirent_t *)(dbuf + off);
            if (de->rec_len == 0) break;
            if (de->inode != 0 && de->name_len == nlen &&
                memcmp(de->name, name, nlen) == 0) {
                de->inode = 0; /* mark deleted */
                ffs_write_blk(v, dblk, dbuf);
                return 0;
            }
            off += de->rec_len;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Path resolution                                                      */
/* ------------------------------------------------------------------ */

/*
 * Resolve an absolute path from root (inode 1).
 * Returns inode number or 0 on error.
 * If `parent_out` is non-NULL, fills it with the parent inode number
 * (useful for create/mkdir).
 */
static uint32_t ffs_path_lookup(ffs_vol_t *v, const char *path,
                                 uint32_t *parent_out) {
    /* Start at root inode (1) */
    uint32_t cur_ino = 1;
    ffs_inode_t cur;
    if (ffs_read_inode(v, cur_ino, &cur) != 0) return 0;

    while (*path == '/') path++;
    if (*path == '\0') {
        if (parent_out) *parent_out = cur_ino;
        return cur_ino;
    }

    while (*path) {
        char comp[256];
        uint32_t ci = 0;
        while (*path && *path != '/' && ci < 255) comp[ci++] = *path++;
        comp[ci] = '\0';
        while (*path == '/') path++;

        if (parent_out) *parent_out = cur_ino;

        uint32_t next = ffs_dir_lookup(v, &cur, comp);
        if (next == 0) return 0;
        cur_ino = next;
        if (ffs_read_inode(v, cur_ino, &cur) != 0) return 0;
    }
    return cur_ino;
}

/* ------------------------------------------------------------------ */
/* VFS backend context                                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    ffs_vol_t  *vol;
    uint32_t    ino;
    ffs_inode_t inode;
    uint64_t    offset;
} ffs_ctx_t;

/* ------------------------------------------------------------------ */
/* VFS ops                                                              */
/* ------------------------------------------------------------------ */

static vfs_bctx_t ffs_vfs_open(vfs_backend_t *be, const char *path, int flags) {
    ffs_vol_t *v = (ffs_vol_t *)be->ctx;
    if (!v) return NULL;

    /* Resolve existing entry */
    uint32_t parent = 0;
    uint32_t ino = ffs_path_lookup(v, path, &parent);

    if (ino == 0) {
        /* Create if requested */
        if (!(flags & VFS_O_CREAT)) return NULL;
        if (!parent) return NULL;

        /* Extract file name */
        const char *slash = path;
        const char *p = path;
        while (*p) { if (*p == '/') slash = p; p++; }
        const char *fname = (*slash == '/') ? slash + 1 : path;
        if (!fname[0]) return NULL;

        ino = ffs_alloc_inode(v);
        if (!ino) return NULL;

        ffs_inode_t ni;
        memset(&ni, 0, sizeof(ni));
        ni.mode  = FFS_IFREG | 0644u;
        ni.nlink = 1;
        if (ffs_write_inode(v, ino, &ni) != 0) {
            ffs_free_inode(v, ino);
            return NULL;
        }

        ffs_inode_t pdir;
        if (ffs_read_inode(v, parent, &pdir) != 0) {
            ffs_free_inode(v, ino);
            return NULL;
        }
        if (ffs_dir_add(v, parent, &pdir, fname, ino, 1) != 0) {
            ffs_free_inode(v, ino);
            return NULL;
        }
        ffs_write_inode(v, parent, &pdir);
    }

    ffs_inode_t inode;
    if (ffs_read_inode(v, ino, &inode) != 0) return NULL;

    if ((inode.mode & FFS_IFDIR) && !(flags & VFS_O_DIR)) return NULL;
    if (!(inode.mode & FFS_IFDIR) && (flags & VFS_O_DIR)) return NULL;

    if ((flags & VFS_O_TRUNC) && (inode.mode & FFS_IFREG)) {
        /* Free all data blocks */
        for (int i = 0; i < (int)FFS_DIRECT_BLOCKS; i++) {
            if (inode.blocks[i]) {
                ffs_free_data_block(v, inode.blocks[i]);
                inode.blocks[i] = 0;
            }
        }
        if (inode.indirect_block) {
            static uint32_t ptrs[FFS_PTRS_PER_BLK];
            if (ffs_read_blk(v, inode.indirect_block, ptrs) == 0) {
                for (uint32_t i = 0; i < FFS_PTRS_PER_BLK; i++)
                    if (ptrs[i]) ffs_free_data_block(v, ptrs[i]);
            }
            ffs_free_data_block(v, inode.indirect_block);
            inode.indirect_block = 0;
        }
        inode.size = 0;
        ffs_write_inode(v, ino, &inode);
    }

    ffs_ctx_t *ctx = (ffs_ctx_t *)kmalloc(sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->vol    = v;
    ctx->ino    = ino;
    ctx->inode  = inode;
    ctx->offset = 0;
    return (vfs_bctx_t)ctx;
}

static void ffs_vfs_close(vfs_backend_t *be, vfs_bctx_t bctx) {
    (void)be;
    if (bctx) kfree(bctx);
}

static int ffs_vfs_read(vfs_backend_t *be, vfs_bctx_t bctx,
                         void *buf, size_t n, size_t *got) {
    (void)be;
    ffs_ctx_t *ctx = (ffs_ctx_t *)bctx;
    if (!ctx || !buf) return VFS_EINVAL;
    if (ctx->inode.mode & FFS_IFDIR) return VFS_EISDIR;

    if (ctx->offset >= ctx->inode.size) {
        if (got) *got = 0;
        return 0;
    }
    size_t avail = (size_t)(ctx->inode.size - ctx->offset);
    if (n > avail) n = avail;

    size_t done = 0;
    static uint8_t rbuf[FFS_BLOCK_SIZE];
    while (done < n) {
        uint32_t blk_idx = (uint32_t)((ctx->offset + done) / FFS_BLOCK_SIZE);
        uint32_t blk_off = (uint32_t)((ctx->offset + done) % FFS_BLOCK_SIZE);
        uint32_t dblk = ffs_file_block(ctx->vol, &ctx->inode, blk_idx, 0);
        if (!dblk) break;
        if (ffs_read_blk(ctx->vol, dblk, rbuf) != 0) break;
        size_t chunk = FFS_BLOCK_SIZE - blk_off;
        if (chunk > n - done) chunk = n - done;
        memcpy((uint8_t *)buf + done, rbuf + blk_off, chunk);
        done += chunk;
    }
    ctx->offset += done;
    if (got) *got = done;
    return 0;
}

static int ffs_vfs_write(vfs_backend_t *be, vfs_bctx_t bctx,
                          const void *buf, size_t n) {
    (void)be;
    ffs_ctx_t *ctx = (ffs_ctx_t *)bctx;
    if (!ctx || !buf || n == 0) return VFS_EINVAL;
    if (ctx->inode.mode & FFS_IFDIR) return VFS_EISDIR;

    size_t done = 0;
    static uint8_t wbuf[FFS_BLOCK_SIZE];
    while (done < n) {
        uint32_t blk_idx = (uint32_t)((ctx->offset + done) / FFS_BLOCK_SIZE);
        uint32_t blk_off = (uint32_t)((ctx->offset + done) % FFS_BLOCK_SIZE);
        uint32_t dblk = ffs_file_block(ctx->vol, &ctx->inode, blk_idx, 1);
        if (!dblk) break;

        memset(wbuf, 0, FFS_BLOCK_SIZE);
        if (blk_off > 0 || (n - done) < FFS_BLOCK_SIZE - blk_off)
            ffs_read_blk(ctx->vol, dblk, wbuf); /* partial write: read first */

        size_t chunk = FFS_BLOCK_SIZE - blk_off;
        if (chunk > n - done) chunk = n - done;
        memcpy(wbuf + blk_off, (const uint8_t *)buf + done, chunk);
        if (ffs_write_blk(ctx->vol, dblk, wbuf) != 0) break;
        done += chunk;
    }
    ctx->offset += done;
    if (ctx->offset > ctx->inode.size)
        ctx->inode.size = ctx->offset;
    ffs_write_inode(ctx->vol, ctx->ino, &ctx->inode);
    return (done == n) ? 0 : VFS_ENOSPC;
}

static int64_t ffs_vfs_seek(vfs_backend_t *be, vfs_bctx_t bctx,
                             int64_t off, int whence) {
    (void)be;
    ffs_ctx_t *ctx = (ffs_ctx_t *)bctx;
    if (!ctx) return VFS_EINVAL;
    int64_t base;
    switch (whence) {
    case VFS_SEEK_SET: base = 0; break;
    case VFS_SEEK_CUR: base = (int64_t)ctx->offset; break;
    case VFS_SEEK_END: base = (int64_t)ctx->inode.size; break;
    default: return VFS_EINVAL;
    }
    int64_t noff = base + off;
    if (noff < 0) return VFS_EINVAL;
    ctx->offset = (uint64_t)noff;
    return noff;
}

static int ffs_vfs_readdir(vfs_backend_t *be, vfs_bctx_t bctx,
                            vfs_dirent_t *buf, uint32_t cap, uint32_t *n_out) {
    (void)be;
    ffs_ctx_t *ctx = (ffs_ctx_t *)bctx;
    if (!ctx || !buf || !n_out) return VFS_EINVAL;
    if (!(ctx->inode.mode & FFS_IFDIR)) return VFS_ENOTDIR;

    uint32_t count = 0;
    uint64_t pos = 0;
    uint64_t dir_size = ctx->inode.size;
    static uint8_t dbuf[FFS_BLOCK_SIZE];

    while (pos < dir_size && count < cap) {
        uint32_t blk_idx = (uint32_t)(pos / FFS_BLOCK_SIZE);
        uint32_t dblk = ffs_file_block(ctx->vol, &ctx->inode, blk_idx, 0);
        if (!dblk) break;
        if (ffs_read_blk(ctx->vol, dblk, dbuf) != 0) break;

        uint32_t off = 0;
        while (off + FFS_DIRENT_HDR <= FFS_BLOCK_SIZE && count < cap) {
            ffs_dirent_t *de = (ffs_dirent_t *)(dbuf + off);
            if (de->rec_len == 0) goto next_block;
            if (de->inode != 0) {
                /* Skip . and .. */
                int is_dot = (de->name_len == 1 && de->name[0] == '.');
                int is_dotdot = (de->name_len == 2 &&
                                 de->name[0] == '.' && de->name[1] == '.');
                if (!is_dot && !is_dotdot) {
                    ffs_inode_t child;
                    uint8_t kind = VFS_NODE_FILE;
                    if (ffs_read_inode(ctx->vol, de->inode, &child) == 0 &&
                        (child.mode & FFS_IFDIR))
                        kind = VFS_NODE_DIR;
                    memset(&buf[count], 0, sizeof(buf[count]));
                    uint8_t nl = de->name_len < VFS_DIRENT_NAME_LEN - 1
                                 ? de->name_len
                                 : (uint8_t)(VFS_DIRENT_NAME_LEN - 1);
                    memcpy(buf[count].name, de->name, nl);
                    buf[count].kind = kind;
                    buf[count].size = (kind == VFS_NODE_FILE)
                                      ? (int64_t)child.size : 0;
                    count++;
                }
            }
            off += de->rec_len;
            pos += de->rec_len;
        }
        /* Advance to next full block boundary */
next_block:
        pos = (uint64_t)(blk_idx + 1) * FFS_BLOCK_SIZE;
    }
    *n_out = count;
    return 0;
}

static int ffs_vfs_stat(vfs_backend_t *be, const char *path, stat_info_t *out) {
    ffs_vol_t *v = (ffs_vol_t *)be->ctx;
    if (!v || !path || !out) return VFS_EINVAL;
    uint32_t ino = ffs_path_lookup(v, path, NULL);
    if (!ino) return VFS_ENOENT;
    ffs_inode_t inode;
    if (ffs_read_inode(v, ino, &inode) != 0) return VFS_ERR;
    memset(out, 0, sizeof(*out));
    out->size  = (int64_t)inode.size;
    out->kind  = (inode.mode & FFS_IFDIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->uid   = (uint16_t)inode.uid;
    out->gid   = (uint16_t)inode.gid;
    /* Copy base name from path */
    const char *slash = path;
    const char *p = path;
    while (*p) { if (*p == '/') slash = p; p++; }
    const char *base = (*slash == '/') ? slash + 1 : path;
    if (!base[0]) base = "/";
    strncpy(out->name, base, VFS_PATH_MAX - 1);
    return 0;
}

static int ffs_vfs_mkdir(vfs_backend_t *be, const char *path) {
    ffs_vol_t *v = (ffs_vol_t *)be->ctx;
    if (!v || !path) return VFS_EINVAL;

    /* Check it doesn't already exist */
    uint32_t parent = 0;
    uint32_t existing = ffs_path_lookup(v, path, &parent);
    if (existing) return 0; /* idempotent */
    if (!parent) return VFS_ENOENT;

    /* Extract name */
    const char *slash = path;
    const char *p = path;
    while (*p) { if (*p == '/') slash = p; p++; }
    const char *dname = (*slash == '/') ? slash + 1 : path;
    if (!dname[0]) return VFS_EINVAL;

    uint32_t ino = ffs_alloc_inode(v);
    if (!ino) return VFS_ENOSPC;

    ffs_inode_t ni;
    memset(&ni, 0, sizeof(ni));
    ni.mode  = FFS_IFDIR | 0755u;
    ni.nlink = 2;

    /* Add . and .. entries */
    if (ffs_dir_add(v, ino, &ni, ".", ino, 2) != 0 ||
        ffs_dir_add(v, ino, &ni, "..", parent, 2) != 0) {
        ffs_free_inode(v, ino);
        return VFS_ENOSPC;
    }
    if (ffs_write_inode(v, ino, &ni) != 0) {
        ffs_free_inode(v, ino);
        return VFS_ERR;
    }

    ffs_inode_t pdir;
    if (ffs_read_inode(v, parent, &pdir) != 0) {
        ffs_free_inode(v, ino);
        return VFS_ERR;
    }
    if (ffs_dir_add(v, parent, &pdir, dname, ino, 2) != 0) {
        ffs_free_inode(v, ino);
        return VFS_ENOSPC;
    }
    return ffs_write_inode(v, parent, &pdir);
}

static int ffs_vfs_unlink(vfs_backend_t *be, const char *path) {
    ffs_vol_t *v = (ffs_vol_t *)be->ctx;
    if (!v || !path) return VFS_EINVAL;

    uint32_t parent = 0;
    uint32_t ino = ffs_path_lookup(v, path, &parent);
    if (!ino) return VFS_ENOENT;
    if (!parent) return VFS_EPERM;

    ffs_inode_t inode;
    if (ffs_read_inode(v, ino, &inode) != 0) return VFS_ERR;
    if (inode.mode & FFS_IFDIR) return VFS_EISDIR;

    const char *slash = path;
    const char *p = path;
    while (*p) { if (*p == '/') slash = p; p++; }
    const char *fname = (*slash == '/') ? slash + 1 : path;

    ffs_inode_t pdir;
    if (ffs_read_inode(v, parent, &pdir) != 0) return VFS_ERR;
    if (ffs_dir_remove(v, &pdir, fname) != 0) return VFS_ENOENT;
    ffs_write_inode(v, parent, &pdir);

    /* Free data blocks */
    for (int i = 0; i < (int)FFS_DIRECT_BLOCKS; i++)
        if (inode.blocks[i]) ffs_free_data_block(v, inode.blocks[i]);
    if (inode.indirect_block) {
        static uint32_t ptrs[FFS_PTRS_PER_BLK];
        if (ffs_read_blk(v, inode.indirect_block, ptrs) == 0)
            for (uint32_t i = 0; i < FFS_PTRS_PER_BLK; i++)
                if (ptrs[i]) ffs_free_data_block(v, ptrs[i]);
        ffs_free_data_block(v, inode.indirect_block);
    }
    ffs_free_inode(v, ino);
    return 0;
}

static const vfs_ops_t g_ffs_ops = {
    .open    = ffs_vfs_open,
    .close   = ffs_vfs_close,
    .read    = ffs_vfs_read,
    .write   = ffs_vfs_write,
    .seek    = ffs_vfs_seek,
    .readdir = ffs_vfs_readdir,
    .stat    = ffs_vfs_stat,
    .unlink  = ffs_vfs_unlink,
    .mkdir   = ffs_vfs_mkdir,
};

/* ------------------------------------------------------------------ */
/* Format                                                               */
/* ------------------------------------------------------------------ */

int ffs_format(block_dev_t *dev, uint64_t first_lba, uint64_t sector_count) {
    if (!dev || !dev->read || !dev->write || sector_count < 256) return -1;
    uint32_t spb = FFS_BLOCK_SIZE / dev->sector_size;
    if (spb == 0) return -1;

    uint32_t total_blocks = (uint32_t)(sector_count / spb);
    if (total_blocks < 32) return -1;

    /* inode count: 1 per 8 data blocks, min 64, max 65535 */
    uint32_t inode_count = total_blocks / 8;
    if (inode_count < 64) inode_count = 64;
    if (inode_count > 65535) inode_count = 65535;

    uint32_t imap_bytes  = (inode_count + 7) / 8;
    uint32_t imap_blocks = (imap_bytes + FFS_BLOCK_SIZE - 1) / FFS_BLOCK_SIZE;
    uint32_t bmap_bytes_tmp  = (total_blocks + 7) / 8;
    uint32_t bmap_blocks = (bmap_bytes_tmp + FFS_BLOCK_SIZE - 1) / FFS_BLOCK_SIZE;
    uint32_t itbl_blocks = (inode_count + FFS_INODES_PER_BLK - 1) / FFS_INODES_PER_BLK;

    uint32_t imap_start  = 2;
    uint32_t bmap_start  = imap_start + imap_blocks;
    uint32_t itbl_start  = bmap_start + bmap_blocks;
    uint32_t data_start  = itbl_start + itbl_blocks;

    if (data_start + 2 >= total_blocks) return -1;

    ffs_superblock_t sb;
    memset(&sb, 0, sizeof(sb));
    sb.magic               = FFS_MAGIC;
    sb.block_size          = FFS_BLOCK_SIZE;
    sb.total_blocks        = total_blocks;
    sb.inode_count         = inode_count;
    sb.free_inodes         = inode_count - 2; /* idx 0 reserved, idx 1 = root */
    sb.free_blocks         = total_blocks - data_start - 1; /* -1 for root dir block */
    sb.inode_bitmap_start  = imap_start;
    sb.inode_bitmap_blocks = imap_blocks;
    sb.block_bitmap_start  = bmap_start;
    sb.block_bitmap_blocks = bmap_blocks;
    sb.inode_table_start   = itbl_start;
    sb.data_start          = data_start;

    /* Write block 0 (reserved boot block) */
    static uint8_t zbuf[FFS_BLOCK_SIZE];
    memset(zbuf, 0, FFS_BLOCK_SIZE);
    {
        uint64_t lba = first_lba;
        for (uint32_t i = 0; i < spb; i++)
            dev->write(dev, lba + i, 1, zbuf + i * dev->sector_size);
    }

    /* Write superblock at block 1 */
    memset(zbuf, 0, FFS_BLOCK_SIZE);
    memcpy(zbuf, &sb, sizeof(sb));
    {
        uint64_t lba = first_lba + (uint64_t)1 * spb;
        dev->write(dev, lba, spb, zbuf);
    }

    /* Zero inode bitmap blocks */
    memset(zbuf, 0, FFS_BLOCK_SIZE);
    for (uint32_t i = 0; i < imap_blocks; i++) {
        uint64_t lba = first_lba + (uint64_t)(imap_start + i) * spb;
        dev->write(dev, lba, spb, zbuf);
    }
    /* Zero block bitmap blocks */
    for (uint32_t i = 0; i < bmap_blocks; i++) {
        uint64_t lba = first_lba + (uint64_t)(bmap_start + i) * spb;
        dev->write(dev, lba, spb, zbuf);
    }
    /* Zero inode table */
    for (uint32_t i = 0; i < itbl_blocks; i++) {
        uint64_t lba = first_lba + (uint64_t)(itbl_start + i) * spb;
        dev->write(dev, lba, spb, zbuf);
    }

    /* Set up a temporary vol to use our alloc helpers */
    ffs_vol_t vol;
    memset(&vol, 0, sizeof(vol));
    vol.dev       = dev;
    vol.first_lba = first_lba;
    vol.spb       = spb;
    vol.sb        = sb;
    vol.imap_bytes = (inode_count + 7) / 8;
    vol.bmap_bytes = (total_blocks + 7) / 8;
    vol.imap = (uint8_t *)kmalloc(vol.imap_bytes);
    vol.bmap = (uint8_t *)kmalloc(vol.bmap_bytes);
    if (!vol.imap || !vol.bmap) {
        if (vol.imap) kfree(vol.imap);
        if (vol.bmap) kfree(vol.bmap);
        return -1;
    }
    memset(vol.imap, 0, vol.imap_bytes);
    memset(vol.bmap, 0, vol.bmap_bytes);

    /* Index 0 is always reserved by bmap_alloc.
     * Inode 1 = root dir — allocate it explicitly. */
    vol.imap[0] |= 0x03; /* bits 0 (reserved) and 1 (root inode) */

    /* Allocate root dir data block (returns >= 1) */
    uint32_t root_blk = ffs_alloc_data_block(&vol);

    /* Create root inode (inode 1) */
    ffs_inode_t root;
    memset(&root, 0, sizeof(root));
    root.mode   = FFS_IFDIR | 0755u;
    root.nlink  = 2;
    root.blocks[0] = root_blk;
    root.size   = FFS_BLOCK_SIZE;

    /* Write . and .. entries in root dir block */
    memset(zbuf, 0, FFS_BLOCK_SIZE);
    {
        ffs_dirent_t *de = (ffs_dirent_t *)zbuf;
        de->inode     = 1;
        de->rec_len   = dirent_len(1);
        de->name_len  = 1;
        de->file_type = 2;
        de->name[0]   = '.';

        ffs_dirent_t *de2 = (ffs_dirent_t *)(zbuf + dirent_len(1));
        de2->inode     = 1;
        de2->rec_len   = (uint16_t)(FFS_BLOCK_SIZE - dirent_len(1));
        de2->name_len  = 2;
        de2->file_type = 2;
        de2->name[0]   = '.';
        de2->name[1]   = '.';
    }
    {
        uint64_t lba = first_lba + (uint64_t)root_blk * spb;
        dev->write(dev, lba, spb, zbuf);
    }

    /* Write root inode */
    ffs_write_inode(&vol, 1, &root);

    /* Flush bitmaps and superblock */
    ffs_imap_flush(&vol);
    ffs_bmap_flush(&vol);
    ffs_sb_flush(&vol);

    kfree(vol.imap);
    kfree(vol.bmap);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Mount                                                                */
/* ------------------------------------------------------------------ */

static int ffs_vol_init(ffs_vol_t *v, block_dev_t *dev,
                        uint64_t first_lba, uint64_t sector_count) {
    uint32_t spb = FFS_BLOCK_SIZE / dev->sector_size;
    if (spb == 0) return -1;
    uint32_t min_secs = 32 * spb;
    if (sector_count < min_secs) return -1;

    v->dev       = dev;
    v->first_lba = first_lba;
    v->spb       = spb;

    /* Read superblock from block 1 */
    static uint8_t sbbuf[FFS_BLOCK_SIZE];
    if (ffs_read_blk(v, 1, sbbuf) != 0) return -1;
    memcpy(&v->sb, sbbuf, sizeof(v->sb));
    if (v->sb.magic != FFS_MAGIC) return -1;

    /* Load bitmaps into memory */
    v->imap_bytes = (v->sb.inode_count + 7) / 8;
    v->bmap_bytes = (v->sb.total_blocks + 7) / 8;
    v->imap = (uint8_t *)kmalloc(v->imap_bytes);
    v->bmap = (uint8_t *)kmalloc(v->bmap_bytes);
    if (!v->imap || !v->bmap) return -1;

    memset(v->imap, 0, v->imap_bytes);
    memset(v->bmap, 0, v->bmap_bytes);

    for (uint32_t i = 0; i < v->sb.inode_bitmap_blocks; i++) {
        uint32_t off = i * FFS_BLOCK_SIZE;
        uint32_t len = v->imap_bytes - off;
        if (len > FFS_BLOCK_SIZE) len = FFS_BLOCK_SIZE;
        static uint8_t tmp[FFS_BLOCK_SIZE];
        if (ffs_read_blk(v, v->sb.inode_bitmap_start + i, tmp) != 0)
            return -1;
        memcpy(v->imap + off, tmp, len);
    }
    for (uint32_t i = 0; i < v->sb.block_bitmap_blocks; i++) {
        uint32_t off = i * FFS_BLOCK_SIZE;
        uint32_t len = v->bmap_bytes - off;
        if (len > FFS_BLOCK_SIZE) len = FFS_BLOCK_SIZE;
        static uint8_t tmp[FFS_BLOCK_SIZE];
        if (ffs_read_blk(v, v->sb.block_bitmap_start + i, tmp) != 0)
            return -1;
        memcpy(v->bmap + off, tmp, len);
    }
    return 0;
}

uint32_t ffs_mount_dev(const char *mount_pt, block_dev_t *dev,
                       uint64_t first_lba, uint64_t sector_count) {
    if (!mount_pt || !dev) return 0;
    if (g_ffs_mounted) return 0; /* only one FFS instance */

    memset(&g_ffs_vol, 0, sizeof(g_ffs_vol));
    if (ffs_vol_init(&g_ffs_vol, dev, first_lba, sector_count) != 0)
        return 0;

    uint32_t id = vfs_mount(mount_pt, g_ffs_ops, &g_ffs_vol);
    if (id) g_ffs_mounted = 1;
    return id;
}

/* Legacy init: read from block 1 at LBA 0 */
void ffs_init(block_dev_t *dev) {
    if (!dev) return;
    ffs_mount_dev("/", dev, 0, dev->sector_count);
}
