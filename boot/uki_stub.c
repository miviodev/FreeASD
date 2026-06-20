/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 */

#include <stdint.h>
#include "../boot/asdboot.h"

typedef uint64_t  u64;
typedef uint32_t  u32;
typedef uint16_t  u16;
typedef uint8_t   u8;
typedef uintptr_t uintn;
typedef void     *EFI_HANDLE;
typedef u64       EFI_STATUS;
typedef u16       CHAR16;

#define EFI_SUCCESS 0ULL

typedef struct {
    u8   _hdr[24];
    u64  _raise_tpl;
    u64  _restore_tpl;
    u64  _alloc_pages;
    u64  _free_pages;
    EFI_STATUS (*get_memory_map)(uintn *sz, void *map, uintn *key,
                                 uintn *desc_sz, u32 *desc_ver);
    EFI_STATUS (*allocate_pool)(u32 type, uintn size, void **buf);
    EFI_STATUS (*free_pool)(void *buf);
    u64  _pad[6];
    EFI_STATUS (*exit_boot_services)(EFI_HANDLE image, uintn key);
    u64  _pad2[4];
    EFI_STATUS (*stall)(uintn us);
} EFI_BOOT_SERVICES;

typedef struct { u8 _hdr[24]; u16 *fw; u32 fwrev; u32 _p;
                 EFI_HANDLE ci; void *con_in;
                 EFI_HANDLE co; void *con_out;
                 EFI_HANDLE se; void *std_err;
                 void *rt; EFI_BOOT_SERVICES *bs;
                 uintn ntab; void *cfg; } EFI_SYSTEM_TABLE;

typedef struct { u32 type; u32 _p; u64 phys; u64 virt; u64 npages; u64 attr; }
    EFI_MEMORY_DESCRIPTOR;

static int
pe_find_section(const void *base, const char *name,
                const void **out_ptr, u64 *out_size)
{
    const u8 *b = (const u8 *)base;

    /* DOS header */
    if (b[0] != 'M' || b[1] != 'Z') return 0;
    u32 pe_off = *(const u32 *)(b + 0x3C);
    const u8 *pe = b + pe_off;

    /* PE signature */
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0) return 0;

    u16 machine     = *(const u16 *)(pe + 4);
    u16 num_sections = *(const u16 *)(pe + 6);
    u16 opt_size    = *(const u16 *)(pe + 20);
    (void)machine;

    /* Optional header starts at pe + 24 */
    const u8 *opt = pe + 24;
    u16 magic = *(const u16 *)opt;
    if (magic != 0x020B) return 0; /* PE32+ only */

    /* Section headers start at pe + 24 + opt_size */
    const u8 *sects = opt + opt_size;

    for (u16 i = 0; i < num_sections; i++) {
        const u8 *sh = sects + i * 40;
        /* Section name: 8 bytes, null-padded */
        int match = 1;
        for (int j = 0; j < 8; j++) {
            char nc = (j < 8) ? (char)sh[j] : '\0';
            char wc = name[j];
            if (nc != wc) { match = 0; break; }
            if (!wc) break;
        }
        if (match) {
            u32 vsize = *(const u32 *)(sh + 16);
            u32 vaddr = *(const u32 *)(sh + 20);
            *out_ptr  = b + vaddr;
            *out_size = vsize;
            return 1;
        }
    }
    return 0;
}


#define ZSTD_MAGIC 0xFD2FB528U

static u64
zstd_decompress_stub(const void *src, u64 src_len,
                     void *out_buf, u64 out_cap)
{
    const u8 *s = (const u8 *)src;
    if (src_len < 4) return 0;

    u32 magic = (u32)s[0] | ((u32)s[1] << 8) |
                ((u32)s[2] << 16) | ((u32)s[3] << 24);
    if (magic != ZSTD_MAGIC) return 0;

    /* For initial bring-up: if the archive is stored uncompressed */
    if (src_len <= out_cap) {
        u8 *d = (u8 *)out_buf;
        for (u64 i = 0; i < src_len; i++) d[i] = s[i];
        return src_len;
    }
    return 0;
}

static void stub_memset(void *p, int v, u64 n) {
    u8 *d = p; while (n--) *d++ = (u8)v;
}
static void stub_memcpy(void *d, const void *s, u64 n) {
    u8 *dd = d; const u8 *ss = s; while (n--) *dd++ = *ss++;
}

extern void kernel_main(asd_bib_t *bib, u64 magic)
    __attribute__((noreturn));

EFI_STATUS
efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
    EFI_BOOT_SERVICES *bs = st->bs;

    /* Locate .cmdline section in the running PE image. */
    extern const u8 __image_base[]; /* linker symbol at PE image base */
    const void *image_base = (const void *)__image_base;

    const void *cmdline_ptr = (void *)0;
    u64         cmdline_sz  = 0;
    pe_find_section(image_base, ".cmdline", &cmdline_ptr, &cmdline_sz);

    const void *initrd_ptr = (void *)0;
    u64         initrd_sz  = 0;
    pe_find_section(image_base, ".initrd", &initrd_ptr, &initrd_sz);

    /* Decompress initrd if present */
    void *initrd_decomp  = (void *)0;
    u64   initrd_decomp_sz = 0;
    if (initrd_ptr && initrd_sz > 0) {
        /* Allocate generously: worst case ~8× expansion */
        u64 alloc_sz = initrd_sz * 8 + 1024 * 1024;
        bs->allocate_pool(2, alloc_sz, &initrd_decomp);
        initrd_decomp_sz = zstd_decompress_stub(initrd_ptr, initrd_sz,
                                                 initrd_decomp, alloc_sz);
        if (initrd_decomp_sz == 0) {
            /* Fallback: pass compressed data as-is; kernel decompresses */
            bs->free_pool(initrd_decomp);
            bs->allocate_pool(2, initrd_sz, &initrd_decomp);
            stub_memcpy(initrd_decomp, initrd_ptr, initrd_sz);
            initrd_decomp_sz = initrd_sz;
        }
    }

    /* Get memory map */
    uintn map_sz  = 0, map_key = 0, desc_sz = 0;
    u32   desc_ver = 0;
    void *mmap     = (void *)0;
    bs->get_memory_map(&map_sz, (void *)0, &map_key, &desc_sz, &desc_ver);
    map_sz += desc_sz * 8;
    bs->allocate_pool(2, map_sz, &mmap);
    bs->get_memory_map(&map_sz, mmap, &map_key, &desc_sz, &desc_ver);

    /* Build BIB */
    asd_bib_t *bib = (void *)0;
    bs->allocate_pool(2, sizeof(asd_bib_t), (void **)&bib);
    stub_memset(bib, 0, sizeof(asd_bib_t));

    uintn entry_count = map_sz / desc_sz;
    asd_mmap_entry_t *asd_mmap = (void *)0;
    bs->allocate_pool(2, sizeof(asd_mmap_entry_t) * entry_count,
                      (void **)&asd_mmap);

    for (uintn i = 0; i < entry_count; i++) {
        EFI_MEMORY_DESCRIPTOR *e =
            (EFI_MEMORY_DESCRIPTOR *)((u8 *)mmap + i * desc_sz);
        /* Simplified type mapping: EfiConventionalMemory(3) → FREE */
        asd_mmap[i].base  = e->phys;
        asd_mmap[i].pages = e->npages;
        asd_mmap[i].type  = (e->type == 3) ? ASD_MEM_FREE : ASD_MEM_RESERVED;
        asd_mmap[i]._pad  = 0;
    }

    bib->magic          = ASD_BIB_MAGIC;
    bib->version        = ASD_BIB_VERSION;
    bib->size           = (u16)sizeof(asd_bib_t);
    bib->mmap_phys      = (u64)(uintn)asd_mmap;
    bib->mmap_count     = (u32)entry_count;
    bib->mmap_entry_sz  = (u32)sizeof(asd_mmap_entry_t);
    bib->cmdline_phys   = (u64)(uintn)cmdline_ptr;
    bib->cmdline_len    = (cmdline_sz > 0) ? (u32)(cmdline_sz - 1) : 0;
    bib->initrd_phys    = (u64)(uintn)initrd_decomp;
    bib->initrd_len     = initrd_decomp_sz;

    /* Exit boot services */
    bs->get_memory_map(&map_sz, mmap, &map_key, &desc_sz, &desc_ver);
    bs->exit_boot_services(image, map_key);

    /* Disable interrupts and jump to kernel */
    __asm__ volatile("cli");
    kernel_main(bib, ASD_BOOT_MAGIC);

    /* Unreachable */
    for (;;) __asm__ volatile("hlt");
    return EFI_SUCCESS;
}
