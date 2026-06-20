#include "fat32.h"
#include "vfs.h"
#include "../mm/mm.h"

#include <stddef.h>
#include <stdint.h>

#define FAT32_DIR_ENTRY_SIZE 32u
#define FAT32_ATTR_DIR       0x10u
#define FAT32_ATTR_LFN       0x0Fu
#define FAT32_CLUSTER_EOC    0x0FFFFFF8u

typedef struct {
    uint8_t *image;
    size_t image_size;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t fats;
    uint32_t fat_size_sectors;
    uint32_t total_sectors;
    uint32_t root_cluster;
    uint32_t data_start_sector;
    uint32_t cluster_count;
    uint8_t  fat16;
    uint32_t root_dir_sector;
    uint32_t root_dir_bytes;
} fat32_fs_t;

typedef struct {
    char name[64];
    uint8_t is_dir;
    uint32_t start_cluster;
    uint32_t size;
} fat32_node_t;

typedef struct {
    fat32_fs_t *fs;
    fat32_node_t node;
    uint32_t offset;
} fat32_file_t;

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void f_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void f_memset(void *dst, int c, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    while (n--) *d++ = (uint8_t)c;
}

static size_t f_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int f_stricmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

static int cluster_is_valid(const fat32_fs_t *fs, uint32_t cluster) {
    return cluster >= 2 && cluster < (fs->cluster_count + 2);
}

static size_t cluster_offset_bytes(const fat32_fs_t *fs, uint32_t cluster) {
    uint32_t cluster_index = cluster - 2;
    uint64_t sector = (uint64_t)fs->data_start_sector +
                      (uint64_t)cluster_index * fs->sectors_per_cluster;
    return (size_t)(sector * fs->bytes_per_sector);
}

static uint32_t fat_next_cluster(const fat32_fs_t *fs, uint32_t cluster) {
    uint64_t fat_base = (uint64_t)fs->reserved_sectors * fs->bytes_per_sector;
    if (fs->fat16) {
        uint64_t off = fat_base + (uint64_t)cluster * 2u;
        if (off + 2 > fs->image_size) return FAT32_CLUSTER_EOC;
        uint16_t v = rd16(fs->image + off);
        if (v >= 0xFFF8u) return FAT32_CLUSTER_EOC;
        return (uint32_t)v;
    }
    uint64_t off = fat_base + (uint64_t)cluster * 4u;
    if (off + 4 > fs->image_size) return FAT32_CLUSTER_EOC;
    return rd32(fs->image + off) & 0x0FFFFFFFu;
}

static void format_83_name(const uint8_t *entry, char *out, size_t out_sz) {
    size_t oi = 0;
    for (size_t i = 0; i < 8 && oi + 1 < out_sz; i++) {
        uint8_t c = entry[i];
        if (c == ' ') break;
        out[oi++] = (char)c;
    }

    int has_ext = 0;
    for (size_t i = 8; i < 11; i++) {
        if (entry[i] != ' ') {
            has_ext = 1;
            break;
        }
    }
    if (has_ext && oi + 1 < out_sz) out[oi++] = '.';
    for (size_t i = 8; i < 11 && oi + 1 < out_sz; i++) {
        uint8_t c = entry[i];
        if (c == ' ') break;
        out[oi++] = (char)c;
    }
    out[oi] = '\0';
}

/* FIX: Decode a LFN (Long File Name) entry into a UTF-8-ish ASCII buffer.
 * LFN entries store 13 UTF-16LE characters each across three fields:
 *   bytes  1-10  (5 chars), bytes 14-21 (6 chars), bytes 28-31 (2 chars).
 * We only keep the low byte of each UTF-16 code unit (ASCII subset). */
static void lfn_extract(const uint8_t *e, char *buf, size_t buf_sz, size_t *len) {
    static const uint8_t offsets[] = {1,3,5,7,9, 14,16,18,20,22,24, 28,30};
    for (int k = 0; k < 13 && *len + 1 < buf_sz; k++) {
        uint16_t ch = (uint16_t)e[offsets[k]] | ((uint16_t)e[offsets[k]+1] << 8);
        if (ch == 0x0000 || ch == 0xFFFF) break;
        buf[(*len)++] = (char)(ch & 0x7F ? ch & 0xFF : '?');
    }
}

static int iter_root_dir_find(const fat32_fs_t *fs, const char *want, fat32_node_t *out) {
    uint32_t bps = fs->bytes_per_sector;
    uint32_t off = 0;
    char lfn_buf[256];
    size_t lfn_len = 0;

    while (off + FAT32_DIR_ENTRY_SIZE <= fs->root_dir_bytes) {
        uint64_t byte_off = (uint64_t)fs->root_dir_sector * bps + off;
        if (byte_off + FAT32_DIR_ENTRY_SIZE > fs->image_size) return 0;
        const uint8_t *e = fs->image + byte_off;
        off += FAT32_DIR_ENTRY_SIZE;

        if (e[0] == 0x00) return 0;
        if (e[0] == 0xE5) { lfn_len = 0; continue; }
        if (e[11] == FAT32_ATTR_LFN) {
            uint8_t seq = e[0];
            if (seq & 0x40) {
                lfn_len = 0;
                lfn_extract(e, lfn_buf, sizeof(lfn_buf), &lfn_len);
            } else {
                char tmp[256];
                size_t tmp_len = 0;
                lfn_extract(e, tmp, sizeof(tmp), &tmp_len);
                if (tmp_len + lfn_len < sizeof(lfn_buf)) {
                    for (size_t x = lfn_len; x > 0; x--)
                        lfn_buf[tmp_len + x - 1] = lfn_buf[x - 1];
                    for (size_t x = 0; x < tmp_len; x++)
                        lfn_buf[x] = tmp[x];
                    lfn_len += tmp_len;
                }
            }
            lfn_buf[lfn_len] = '\0';
            continue;
        }

        char name83[64];
        format_83_name(e, name83, sizeof(name83));
        const char *match_name = (lfn_len > 0) ? lfn_buf : name83;
        int matched = 0;
        if (!want) {
            matched = 1;
        } else {
            if (f_stricmp(match_name, want) == 0) matched = 1;
            if (!matched && f_stricmp(name83, want) == 0) matched = 1;
        }
        if (!matched) {
            lfn_len = 0;
            continue;
        }

        if (out) {
            f_memset(out, 0, sizeof(*out));
            const char *stored = (lfn_len > 0) ? lfn_buf : name83;
            for (size_t i = 0; i < sizeof(out->name) - 1 && stored[i]; i++)
                out->name[i] = stored[i];
            out->is_dir = (e[11] & FAT32_ATTR_DIR) ? 1u : 0u;
            out->start_cluster = rd16(e + 26);
            out->size = rd32(e + 28);
        }
        lfn_len = 0;
        return 1;
    }
    return 0;
}

static int iter_dir_find(const fat32_fs_t *fs, uint32_t dir_cluster,
                         const char *want, fat32_node_t *out)
{
    if (fs->fat16 && dir_cluster == 0)
        return iter_root_dir_find(fs, want, out);
    if (!cluster_is_valid(fs, dir_cluster)) return 0;
    uint32_t cluster = dir_cluster;
    uint32_t cluster_bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
    /* Accumulate LFN parts as we scan; cleared on each new 8.3 entry. */
    char lfn_buf[256];
    size_t lfn_len = 0;
    while (cluster_is_valid(fs, cluster) && cluster < FAT32_CLUSTER_EOC) {
        size_t coff = cluster_offset_bytes(fs, cluster);
        if (coff + cluster_bytes > fs->image_size) return 0;
        const uint8_t *p = fs->image + coff;
        for (uint32_t off = 0; off + FAT32_DIR_ENTRY_SIZE <= cluster_bytes; off += FAT32_DIR_ENTRY_SIZE) {
            const uint8_t *e = p + off;
            if (e[0] == 0x00) return 0;
            if (e[0] == 0xE5) { lfn_len = 0; continue; }
            if (e[11] == FAT32_ATTR_LFN) {
                /* Accumulate LFN: entries arrive in reverse order (seq & 0x3F).
                 * Simplest approach: rebuild from scratch each time we see
                 * the last entry (seq & 0x40 set), then append subsequent ones. */
                uint8_t seq = e[0];
                if (seq & 0x40) {
                    /* First LFN entry (last in file order) — reset buffer */
                    lfn_len = 0;
                    lfn_extract(e, lfn_buf, sizeof(lfn_buf), &lfn_len);
                } else {
                    /* Subsequent LFN entry — prepend characters.
                     * We use a simple shift-right approach. */
                    char tmp[256];
                    size_t tmp_len = 0;
                    lfn_extract(e, tmp, sizeof(tmp), &tmp_len);
                    if (tmp_len + lfn_len < sizeof(lfn_buf)) {
                        /* Shift existing content right */
                        for (size_t x = lfn_len; x > 0; x--)
                            lfn_buf[tmp_len + x - 1] = lfn_buf[x - 1];
                        /* Copy new part at start */
                        for (size_t x = 0; x < tmp_len; x++)
                            lfn_buf[x] = tmp[x];
                        lfn_len += tmp_len;
                    }
                }
                lfn_buf[lfn_len] = '\0';
                continue;
            }
            /* 8.3 entry: check both 8.3 name and accumulated LFN */
            char name83[64];
            format_83_name(e, name83, sizeof(name83));
            /* Determine the best name to use for matching */
            const char *match_name = (lfn_len > 0) ? lfn_buf : name83;
            int matched = 0;
            if (!want) {
                matched = 1;
            } else {
                if (f_stricmp(match_name, want) == 0) matched = 1;
                /* Also try 8.3 name as fallback */
                if (!matched && f_stricmp(name83, want) == 0) matched = 1;
            }
            lfn_len = 0; /* reset LFN accumulator for next entry */
            if (!matched) continue;
            if (out) {
                f_memset(out, 0, sizeof(*out));
                const char *use_name = (lfn_len == 0 && match_name == lfn_buf) ? lfn_buf : name83;
                /* Use LFN if available, otherwise 8.3 */
                const char *stored = (match_name == lfn_buf && lfn_buf[0]) ? lfn_buf : name83;
                for (size_t i = 0; i < sizeof(out->name) - 1 && stored[i]; i++) out->name[i] = stored[i];
                out->is_dir = (e[11] & FAT32_ATTR_DIR) ? 1u : 0u;
                out->start_cluster = ((uint32_t)rd16(e + 20) << 16) | rd16(e + 26);
                if (out->start_cluster == 0 && out->is_dir) out->start_cluster = fs->root_cluster;
                out->size = rd32(e + 28);
                (void)use_name;
            }
            return 1;
        }
        uint32_t next = fat_next_cluster(fs, cluster);
        if (next >= FAT32_CLUSTER_EOC) break;
        if (!cluster_is_valid(fs, next)) break;
        cluster = next;
    }
    return 0;
}

static int resolve_path(const fat32_fs_t *fs, const char *path, fat32_node_t *out) {
    fat32_node_t cur;
    f_memset(&cur, 0, sizeof(cur));
    cur.is_dir = 1;
    cur.start_cluster = fs->fat16 ? 0 : fs->root_cluster;

    while (*path == '/') path++;
    if (*path == '\0') {
        if (out) *out = cur;
        return 1;
    }

    char part[64];
    while (*path) {
        size_t pi = 0;
        while (*path && *path != '/' && pi + 1 < sizeof(part)) part[pi++] = *path++;
        part[pi] = '\0';
        while (*path == '/') path++;

        if (!cur.is_dir) return 0;
        fat32_node_t next;
        if (!iter_dir_find(fs, cur.start_cluster, part, &next)) return 0;
        cur = next;
    }
    if (out) *out = cur;
    return 1;
}

static vfs_bctx_t fat32_open(vfs_backend_t *be, const char *path, int flags) {
    fat32_fs_t *fs = (fat32_fs_t *)be->ctx;
    if (!fs || !path) return NULL;
    if (flags & VFS_O_WRITE) return NULL;

    fat32_node_t n;
    if (!resolve_path(fs, path, &n)) return NULL;
    if ((flags & VFS_O_DIR) && !n.is_dir) return NULL;

    fat32_file_t *f = (fat32_file_t *)kmalloc(sizeof(*f));
    if (!f) return NULL;
    f->fs = fs;
    f->node = n;
    f->offset = 0;
    return (vfs_bctx_t)f;
}

static void fat32_close(vfs_backend_t *be, vfs_bctx_t bctx) {
    (void)be;
    if (bctx) kfree(bctx);
}

static int fat32_read(vfs_backend_t *be, vfs_bctx_t bctx, void *buf, size_t n, size_t *got) {
    (void)be;
    fat32_file_t *f = (fat32_file_t *)bctx;
    if (!f || !buf) return VFS_EINVAL;
    if (f->node.is_dir) return VFS_EISDIR;

    if (f->offset >= f->node.size) {
        if (got) *got = 0;
        return 0;
    }

    size_t remain = f->node.size - f->offset;
    size_t need = (n < remain) ? n : remain;
    size_t done = 0;

    uint32_t cluster_bytes = f->fs->bytes_per_sector * f->fs->sectors_per_cluster;
    uint32_t cluster = f->node.start_cluster;
    if (!cluster_is_valid(f->fs, cluster)) return VFS_ERR;

    uint32_t skip = f->offset / cluster_bytes;
    uint32_t in_cluster = f->offset % cluster_bytes;
    while (skip--) {
        cluster = fat_next_cluster(f->fs, cluster);
        if (cluster >= FAT32_CLUSTER_EOC || !cluster_is_valid(f->fs, cluster)) return VFS_ERR;
    }

    while (done < need) {
        size_t coff = cluster_offset_bytes(f->fs, cluster);
        if (coff + cluster_bytes > f->fs->image_size) return VFS_ERR;

        size_t chunk = cluster_bytes - in_cluster;
        if (chunk > (need - done)) chunk = need - done;
        f_memcpy((uint8_t *)buf + done, f->fs->image + coff + in_cluster, chunk);
        done += chunk;
        in_cluster = 0;

        if (done < need) {
            cluster = fat_next_cluster(f->fs, cluster);
            if (cluster >= FAT32_CLUSTER_EOC || !cluster_is_valid(f->fs, cluster)) break;
        }
    }

    f->offset += (uint32_t)done;
    if (got) *got = done;
    return 0;
}

static int fat32_write(vfs_backend_t *be, vfs_bctx_t bctx, const void *buf, size_t n) {
    (void)be; (void)bctx; (void)buf; (void)n;
    return VFS_EPERM;
}

static int64_t fat32_seek(vfs_backend_t *be, vfs_bctx_t bctx, int64_t off, int whence) {
    (void)be;
    fat32_file_t *f = (fat32_file_t *)bctx;
    if (!f) return VFS_EINVAL;
    int64_t base;
    switch (whence) {
    case VFS_SEEK_SET: base = 0; break;
    case VFS_SEEK_CUR: base = (int64_t)f->offset; break;
    case VFS_SEEK_END: base = (int64_t)f->node.size; break;
    default: return VFS_EINVAL;
    }
    int64_t noff = base + off;
    if (noff < 0 || (uint64_t)noff > f->node.size) return VFS_EINVAL;
    f->offset = (uint32_t)noff;
    return noff;
}

static int fat32_readdir(vfs_backend_t *be, vfs_bctx_t bctx,
                         vfs_dirent_t *buf, uint32_t cap, uint32_t *n_out)
{
    fat32_file_t *f = (fat32_file_t *)bctx;
    fat32_fs_t *fs = (fat32_fs_t *)be->ctx;
    if (!f || !fs || !buf || !n_out) return VFS_EINVAL;
    if (!f->node.is_dir) return VFS_ENOTDIR;

        uint32_t count = 0;
    uint32_t cluster = f->node.start_cluster;
    uint32_t cluster_bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
    char rd_lfn[256]; size_t rd_lfn_len = 0;
    while (cluster_is_valid(fs, cluster) && cluster < FAT32_CLUSTER_EOC && count < cap) {
        size_t coff = cluster_offset_bytes(fs, cluster);
        if (coff + cluster_bytes > fs->image_size) break;
        const uint8_t *p = fs->image + coff;
        for (uint32_t off = 0; off + FAT32_DIR_ENTRY_SIZE <= cluster_bytes && count < cap; off += FAT32_DIR_ENTRY_SIZE) {
            const uint8_t *e = p + off;
            if (e[0] == 0x00) {
                *n_out = count;
                return 0;
            }
            if (e[0] == 0xE5) { rd_lfn_len = 0; continue; }
            if (e[11] == FAT32_ATTR_LFN) {
                uint8_t seq = e[0];
                if (seq & 0x40) {
                    rd_lfn_len = 0;
                    lfn_extract(e, rd_lfn, sizeof(rd_lfn), &rd_lfn_len);
                } else {
                    char tmp2[256]; size_t tmp2_len = 0;
                    lfn_extract(e, tmp2, sizeof(tmp2), &tmp2_len);
                    if (tmp2_len + rd_lfn_len < sizeof(rd_lfn)) {
                        for (size_t x = rd_lfn_len; x > 0; x--) rd_lfn[tmp2_len + x - 1] = rd_lfn[x - 1];
                        for (size_t x = 0; x < tmp2_len; x++) rd_lfn[x] = tmp2[x];
                        rd_lfn_len += tmp2_len;
                    }
                }
                rd_lfn[rd_lfn_len] = '\0';
                continue;
            }
            char name83[64];
            format_83_name(e, name83, sizeof(name83));
            const char *dname = (rd_lfn_len > 0) ? rd_lfn : name83;
            rd_lfn_len = 0;
            f_memset(&buf[count], 0, sizeof(buf[count]));
            for (size_t i = 0; i < VFS_DIRENT_NAME_LEN - 1 && dname[i]; i++) buf[count].name[i] = dname[i];
            buf[count].kind = (e[11] & FAT32_ATTR_DIR) ? VFS_NODE_DIR : VFS_NODE_FILE;
            buf[count].size = (int64_t)rd32(e + 28);
            count++;
        }
        uint32_t next = fat_next_cluster(fs, cluster);
        if (next >= FAT32_CLUSTER_EOC || !cluster_is_valid(fs, next)) break;
        cluster = next;
    }
    *n_out = count;
    return 0;
}
static int fat32_stat(vfs_backend_t *be, const char *path, stat_info_t *out) {
    fat32_fs_t *fs = (fat32_fs_t *)be->ctx;
    if (!fs || !path || !out) return VFS_EINVAL;

    fat32_node_t n;
    if (!resolve_path(fs, path, &n)) return VFS_ENOENT;
    f_memset(out, 0, sizeof(*out));
    out->size = n.is_dir ? 0 : (int64_t)n.size;
    out->kind = n.is_dir ? VFS_NODE_DIR : VFS_NODE_FILE;
    out->mtime_ns = 0;
    if (n.name[0]) {
        for (size_t i = 0; i < VFS_PATH_MAX - 1 && n.name[i]; i++) out->name[i] = n.name[i];
    } else {
        out->name[0] = '/';
        out->name[1] = '\0';
    }
    return 0;
}

static int fat32_unlink(vfs_backend_t *be, const char *path) {
    (void)be; (void)path;
    return VFS_EPERM;
}

static int fat32_mkdir(vfs_backend_t *be, const char *path) {
    (void)be; (void)path;
    return VFS_EPERM;
}

static int fat32_parse_header(fat32_fs_t *fs, const uint8_t *img, size_t sz) {
    if (!fs || !img || sz < 512) return -1;
    if (img[510] != 0x55 || img[511] != 0xAA) return -1;

    uint16_t bps = rd16(img + 11);
    uint8_t spc = img[13];
    uint16_t reserved = rd16(img + 14);
    uint8_t fats = img[16];
    uint32_t total_sectors = rd16(img + 19);
    if (total_sectors == 0) total_sectors = rd32(img + 32);
    uint32_t fat_sz16 = rd16(img + 22);
    uint32_t fat_sz = fat_sz16;
    if (fat_sz == 0) fat_sz = rd32(img + 36);
    uint32_t root_cluster = rd32(img + 44);

    if (bps == 0 || spc == 0 || reserved == 0 || fats == 0 || fat_sz == 0 || total_sectors == 0)
        return -1;

    /* try_mount_fat_volume() loads a prefix (e.g. 64 MiB) of large install disks. */
    uint32_t image_sectors = (uint32_t)(sz / bps);
    if (image_sectors < reserved + fats * fat_sz + spc)
        return -1;

    uint32_t vol_sectors = total_sectors;
    if (vol_sectors > image_sectors)
        total_sectors = image_sectors;

    uint32_t data_start = reserved + fats * fat_sz;
    if (data_start >= total_sectors)
        return -1;
    uint32_t data_sectors = total_sectors - data_start;
    uint32_t clusters = data_sectors / spc;
    if (clusters < 2)
        return -1;

    int is_fat32 = (fat_sz16 == 0 && rd32(img + 36) != 0);

    fs->image = (uint8_t *)img;
    fs->image_size = sz;
    fs->bytes_per_sector = bps;
    fs->sectors_per_cluster = spc;
    fs->reserved_sectors = reserved;
    fs->fats = fats;
    fs->fat_size_sectors = fat_sz;
    fs->total_sectors = total_sectors;
    fs->fat16 = 0;
    fs->root_dir_sector = 0;
    fs->root_dir_bytes = 0;
    fs->data_start_sector = data_start;
    fs->cluster_count = clusters;

    if (is_fat32 && root_cluster >= 2) {
        fs->root_cluster = root_cluster;
        return 0;
    }

    /* FAT12/FAT16 (live.img, small ESP images). */
    if (is_fat32)
        return -1;

    uint16_t root_entries = rd16(img + 17);
    if (root_entries == 0) root_entries = 512;
    fs->fat16 = 1;
    fs->root_cluster = 0;
    fs->root_dir_sector = data_start;
    fs->root_dir_bytes = (uint32_t)root_entries * FAT32_DIR_ENTRY_SIZE;
    return 0;
}

static const vfs_ops_t g_fat32_ops = {
    .open    = fat32_open,
    .close   = fat32_close,
    .read    = fat32_read,
    .write   = fat32_write,
    .seek    = fat32_seek,
    .readdir = fat32_readdir,
    .stat    = fat32_stat,
    .unlink  = fat32_unlink,
    .mkdir   = fat32_mkdir,
};

static void fat_set32(fat32_fs_t *fs, uint32_t cluster, uint32_t value) {
    uint64_t off = (uint64_t)fs->reserved_sectors * fs->bytes_per_sector +
                   (uint64_t)cluster * 4u;
    if (off + 4 > fs->image_size) return;
    uint32_t cur = rd32(fs->image + off);
    wr32(fs->image + (size_t)off, (cur & 0xF0000000u) | (value & 0x0FFFFFFFu));
}

static void fat_set16(fat32_fs_t *fs, uint32_t cluster, uint16_t value) {
    uint64_t off = (uint64_t)fs->reserved_sectors * fs->bytes_per_sector +
                   (uint64_t)cluster * 2u;
    if (off + 2 > fs->image_size) return;
    wr16(fs->image + (size_t)off, value);
}

static uint32_t fat_find_free_cluster(fat32_fs_t *fs) {
    uint32_t maxc = fs->cluster_count + 2;
    for (uint32_t c = 2; c < maxc; c++) {
        if (fs->fat16) {
            uint64_t off = (uint64_t)fs->reserved_sectors * fs->bytes_per_sector +
                           (uint64_t)c * 2u;
            if (off + 2 > fs->image_size) break;
            if (rd16(fs->image + off) == 0) return c;
        } else {
            uint64_t off = (uint64_t)fs->reserved_sectors * fs->bytes_per_sector +
                           (uint64_t)c * 4u;
            if (off + 4 > fs->image_size) break;
            if ((rd32(fs->image + off) & 0x0FFFFFFFu) == 0) return c;
        }
    }
    return 0;
}

static int fat_write_root_entry(fat32_fs_t *fs, const char *name83,
                                uint32_t cluster, uint32_t size) {
    char want[13];
    for (int i = 0; i < 8; i++) want[i] = (name83 && name83[i]) ? name83[i] : ' ';
    want[8] = '\0';

    uint32_t dir_cluster = fs->fat16 ? 0 : fs->root_cluster;
    if (fs->fat16) {
        uint32_t bps = fs->bytes_per_sector;
        for (uint32_t off = 0; off + FAT32_DIR_ENTRY_SIZE <= fs->root_dir_bytes;
             off += FAT32_DIR_ENTRY_SIZE) {
            uint64_t byte_off = (uint64_t)fs->root_dir_sector * bps + off;
            if (byte_off + FAT32_DIR_ENTRY_SIZE > fs->image_size) return -1;
            uint8_t *e = fs->image + byte_off;
            if (e[0] == 0x00 || e[0] == 0xE5) {
                f_memset(e, 0, FAT32_DIR_ENTRY_SIZE);
                for (int i = 0; i < 8; i++) e[i] = want[i];
                e[11] = 0x20;
                wr16(e + 26, (uint16_t)cluster);
                wr32(e + 28, size);
                return 0;
            }
            char name[64];
            format_83_name(e, name, sizeof(name));
            if (f_stricmp(name, want) == 0) {
                wr16(e + 26, (uint16_t)cluster);
                wr32(e + 28, size);
                return 0;
            }
        }
        return -1;
    }

    if (!cluster_is_valid(fs, dir_cluster)) return -1;
    uint32_t cluster_bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
    uint32_t c = dir_cluster;
    while (cluster_is_valid(fs, c) && c < FAT32_CLUSTER_EOC) {
        size_t coff = cluster_offset_bytes(fs, c);
        if (coff + cluster_bytes > fs->image_size) return -1;
        uint8_t *p = fs->image + coff;
        for (uint32_t off = 0; off + FAT32_DIR_ENTRY_SIZE <= cluster_bytes;
             off += FAT32_DIR_ENTRY_SIZE) {
            uint8_t *e = p + off;
            if (e[0] == 0x00 || e[0] == 0xE5) {
                f_memset(e, 0, FAT32_DIR_ENTRY_SIZE);
                for (int i = 0; i < 8; i++) e[i] = want[i];
                e[11] = 0x20;
                wr16(e + 26, (uint16_t)(cluster & 0xFFFFu));
                wr16(e + 20, (uint16_t)((cluster >> 16) & 0xFFFFu));
                wr32(e + 28, size);
                return 0;
            }
            if (e[11] == FAT32_ATTR_LFN) continue;
            char name[64];
            format_83_name(e, name, sizeof(name));
            if (f_stricmp(name, want) == 0) {
                wr16(e + 26, (uint16_t)(cluster & 0xFFFFu));
                wr16(e + 20, (uint16_t)((cluster >> 16) & 0xFFFFu));
                wr32(e + 28, size);
                return 0;
            }
        }
        uint32_t next = fat_next_cluster(fs, c);
        if (next >= FAT32_CLUSTER_EOC) break;
        c = next;
    }
    return -1;
}

static int fat_write_file_data(fat32_fs_t *fs, uint32_t cluster,
                               const void *data, size_t data_len) {
    uint32_t cluster_bytes = fs->bytes_per_sector * fs->sectors_per_cluster;
    size_t written = 0;
    uint32_t c = cluster;
    while (written < data_len) {
        if (!cluster_is_valid(fs, c)) return -1;
        size_t coff = cluster_offset_bytes(fs, c);
        if (coff + cluster_bytes > fs->image_size) return -1;
        size_t chunk = cluster_bytes;
        if (chunk > data_len - written) chunk = data_len - written;
        f_memcpy(fs->image + coff, (const uint8_t *)data + written, chunk);
        written += chunk;
        if (written < data_len) {
            uint32_t next = fat_find_free_cluster(fs);
            if (!next) return -1;
            if (fs->fat16) fat_set16(fs, c, (uint16_t)next);
            else           fat_set32(fs, c, next);
            c = next;
        } else {
            if (fs->fat16) fat_set16(fs, c, 0xFFFFu);
            else           fat_set32(fs, c, FAT32_CLUSTER_EOC);
        }
    }
    return 0;
}

int fat32_image_upsert_file(void *image, size_t image_size,
                            const char *name83, const void *data, size_t data_len) {
    if (!image || image_size < 512 || !name83 || !data || data_len == 0)
        return -1;

    fat32_fs_t fs;
    f_memset(&fs, 0, sizeof(fs));
    if (fat32_parse_header(&fs, (const uint8_t *)image, image_size) != 0)
        return -1;

    uint32_t cluster_bytes = fs.bytes_per_sector * fs.sectors_per_cluster;
    if (data_len > cluster_bytes * 16) return -1; /* keep installer writes small */

    fat32_node_t existing;
    int has = iter_dir_find(&fs, fs.fat16 ? 0 : fs.root_cluster, name83, &existing);

    uint32_t cluster;
    if (has && existing.start_cluster >= 2) {
        cluster = existing.start_cluster;
    } else {
        cluster = fat_find_free_cluster(&fs);
        if (!cluster) return -1;
    }

    if (fat_write_file_data(&fs, cluster, data, data_len) != 0)
        return -1;

    if (fat_write_root_entry(&fs, name83, cluster, (uint32_t)data_len) != 0)
        return -1;

    return 0;
}

uint32_t fat32_mount_image(const char *mount_pt, const void *image, size_t image_size) {
    if (!mount_pt || !image || image_size < 512) return 0;

    fat32_fs_t *fs = (fat32_fs_t *)kmalloc(sizeof(*fs));
    if (!fs) return 0;
    f_memset(fs, 0, sizeof(*fs));

    if (fat32_parse_header(fs, (const uint8_t *)image, image_size) != 0) {
        kfree(fs);
        return 0;
    }

    uint32_t id = vfs_mount(mount_pt, g_fat32_ops, fs);
    if (id == 0) {
        kfree(fs);
        return 0;
    }
    return id;
}
