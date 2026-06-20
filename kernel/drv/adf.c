/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 */

#include "adf.h"
#include "../mm/mm.h"
#include "../vfs/vfs.h"
#include <stddef.h>
#include <string.h>

typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

#define EI_NIDENT   16
#define ET_REL      1
#define EM_X86_64   62

typedef struct {
    uint8_t     e_ident[EI_NIDENT];
    Elf64_Half  e_type;
    Elf64_Half  e_machine;
    Elf64_Word  e_version;
    Elf64_Addr  e_entry;
    Elf64_Off   e_phoff;
    Elf64_Off   e_shoff;
    Elf64_Word  e_flags;
    Elf64_Half  e_ehsize;
    Elf64_Half  e_phentsize;
    Elf64_Half  e_phnum;
    Elf64_Half  e_shentsize;
    Elf64_Half  e_shnum;
    Elf64_Half  e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf64_Word  sh_name;
    Elf64_Word  sh_type;
    Elf64_Xword sh_flags;
    Elf64_Addr  sh_addr;
    Elf64_Off   sh_offset;
    Elf64_Xword sh_size;
    Elf64_Word  sh_link;
    Elf64_Word  sh_info;
    Elf64_Xword sh_addralign;
    Elf64_Xword sh_entsize;
} Elf64_Shdr;

typedef struct {
    Elf64_Word  st_name;
    uint8_t     st_info;
    uint8_t     st_other;
    Elf64_Half  st_shndx;
    Elf64_Addr  st_value;
    Elf64_Xword st_size;
} Elf64_Sym;

typedef struct {
    Elf64_Addr   r_offset;
    Elf64_Xword  r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

#define ELF64_R_SYM(i)   ((i) >> 32)
#define ELF64_R_TYPE(i)  ((i) & 0xFFFFFFFF)
#define R_X86_64_64      1
#define R_X86_64_PC32    2
#define R_X86_64_PLT32   4
#define R_X86_64_32      10
#define R_X86_64_32S     11

#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_NOBITS   8

static inline void spin_lock(uint32_t *l) {
    while (__atomic_exchange_n(l, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}
static inline void spin_unlock(uint32_t *l) {
    __atomic_store_n(l, 0, __ATOMIC_RELEASE);
}

extern vaddr_t  asd_shmap(const char *, size_t, int);
extern int      asd_shunmap(vaddr_t);
extern port_t   asd_port_open_k(const char *, int);
extern void     asd_port_close_k(port_t);
extern int      asd_port_send_k(port_t, const void *, size_t);
extern int      asd_port_recv_k(port_t, void *, size_t, size_t *);
extern uint64_t asd_time_ns(void);
extern void *   kmalloc(size_t);
extern void     kfree(void *);

typedef struct { const char *name; void *addr; } ksym_t;

static const ksym_t g_ksyms[] = {
    { "asd_shmap",        (void *)amm_shmap        },
    { "asd_shunmap",      (void *)amm_shunmap       },
    { "asd_port_open",    (void *)port_open         },
    { "asd_port_close",   (void *)port_close        },
    { "asd_port_send",    (void *)port_send         },
    { "asd_port_recv",    (void *)port_recv         },
    { "asd_time_ns",      (void *)asd_time_ns       },
    { "kmalloc",          (void *)kmalloc           },
    { "kfree",            (void *)kfree             },
    { "ringbuf_init",     (void *)ringbuf_init      },
    { "ringbuf_push",     (void *)ringbuf_push      },
    { "ringbuf_pop",      (void *)ringbuf_pop       },
    { NULL, NULL }
};

static void *ksym_lookup(const char *name) {
    for (int i = 0; g_ksyms[i].name; i++)
        if (strcmp(g_ksyms[i].name, name) == 0)
            return g_ksyms[i].addr;
    return NULL;
}

typedef struct {
    uint32_t    mod_id;
    void       *load_base;  /* kmalloc'd buffer holding module code+data */
    size_t      load_size;
    adf_dev_t   dev;
    adf_meta_t  meta;
} module_t;

static module_t  g_modules[MAX_MODULES];
static uint32_t  g_mod_lock;
static uint32_t  g_next_mod_id = 1;

void adf_init(void) {
    memset(g_modules, 0, sizeof(g_modules));
    g_mod_lock    = 0;
    g_next_mod_id = 1;
}

static int read_file(const char *path, void **buf_out, size_t *size_out) {
    /* Open the file (kernel context — no PCB) */
    fd_t fd = vfs_open(path, VFS_O_READ, NULL);
    if (fd == 0 || (int)fd < 0) return -1;

    /* First, get the file size via stat */
    stat_info_t info;
    if (vfs_stat(path, &info) != 0 || info.size <= 0) {
        vfs_close(fd);
        return -1;
    }

    size_t sz = (size_t)info.size;
    void *buf = kmalloc(sz);
    if (!buf) {
        vfs_close(fd);
        return -1;
    }

    /* Read the entire file */
    size_t got = 0;
    size_t total = 0;
    while (total < sz) {
        if (vfs_read(fd, (uint8_t *)buf + total, sz - total, &got) != 0)
            break;
        if (got == 0) break; /* EOF */
        total += got;
    }

    vfs_close(fd);

    if (total == 0) {
        kfree(buf);
        return -1;
    }

    *buf_out  = buf;
    *size_out = total;
    return 0;
}

static int
elf_load(const uint8_t *elf, size_t elf_size,
         void **load_base_out, size_t *load_size_out,
         adf_driver_t **entry_out, adf_meta_t **meta_out)
{
    if (elf_size < sizeof(Elf64_Ehdr)) return -1;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)elf;

    /* Validate ELF magic */
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') return -1;
    if (eh->e_type != ET_REL || eh->e_machine != EM_X86_64) return -1;

    const Elf64_Shdr *shdrs = (const Elf64_Shdr *)(elf + eh->e_shoff);
    const char *shstrtab =
        (const char *)(elf + shdrs[eh->e_shstrndx].sh_offset);

    /* Pass 1: compute total load size (sum of PROGBITS + NOBITS sections) */
    size_t total = 0;
    for (Elf64_Half i = 0; i < eh->e_shnum; i++) {
        const Elf64_Shdr *sh = &shdrs[i];
        if (sh->sh_type != SHT_PROGBITS && sh->sh_type != SHT_NOBITS) continue;
        size_t aligned = (total + (size_t)sh->sh_addralign - 1) &
                         ~((size_t)sh->sh_addralign - 1);
        total = aligned + (size_t)sh->sh_size;
    }

    uint8_t *base = kmalloc(total);
    if (!base) return -1;
    memset(base, 0, total);
    *load_base_out = base;
    *load_size_out = total;

    /* Pass 2: copy sections to their allocated positions */
    /* We store the section load addresses in a side array */
    uint64_t sh_vaddr[256] = {0};
    size_t cursor = 0;

    for (Elf64_Half i = 0; i < eh->e_shnum && i < 256; i++) {
        const Elf64_Shdr *sh = &shdrs[i];
        if (sh->sh_type != SHT_PROGBITS && sh->sh_type != SHT_NOBITS) continue;
        size_t aligned = (cursor + (size_t)sh->sh_addralign - 1) &
                         ~((size_t)sh->sh_addralign - 1);
        cursor = aligned;
        sh_vaddr[i] = (uint64_t)(uintptr_t)(base + cursor);
        if (sh->sh_type == SHT_PROGBITS)
            memcpy(base + cursor, elf + sh->sh_offset, sh->sh_size);
        cursor += sh->sh_size;
    }

    /* Pass 3: find .symtab, .adfmeta, adf_entry symbol */
    const Elf64_Sym *symtab   = NULL;
    size_t           sym_count = 0;
    const char      *strtab   = NULL;
    adf_meta_t      *meta     = NULL;

    for (Elf64_Half i = 0; i < eh->e_shnum; i++) {
        const Elf64_Shdr *sh = &shdrs[i];
        const char *sname = shstrtab + sh->sh_name;
        if (sh->sh_type == SHT_SYMTAB) {
            symtab    = (const Elf64_Sym *)(elf + sh->sh_offset);
            sym_count = sh->sh_size / sizeof(Elf64_Sym);
            strtab    = (const char *)(elf + shdrs[sh->sh_link].sh_offset);
        }
        if (strcmp(sname, ".adfmeta") == 0) {
            /* .adfmeta is a PROGBITS section in the loaded image */
            meta = (adf_meta_t *)(uintptr_t)sh_vaddr[i];
        }
    }

    if (!symtab || !strtab) { kfree(base); return -1; }
    if (!meta || meta->magic != ADF_META_MAGIC) { kfree(base); return -1; }
    *meta_out = meta;

    /* Find adf_entry symbol */
    adf_driver_t *entry = NULL;
    for (size_t i = 0; i < sym_count; i++) {
        if (strcmp(strtab + symtab[i].st_name, "adf_entry") == 0) {
            uint16_t shidx = symtab[i].st_shndx;
            if (shidx < 256 && sh_vaddr[shidx]) {
                entry = (adf_driver_t *)(uintptr_t)
                        (sh_vaddr[shidx] + symtab[i].st_value);
            }
        }
    }
    if (!entry || entry->adf_magic != ADF_ENTRY_MAGIC) {
        kfree(base); return -1;
    }
    *entry_out = entry;

    /* Pass 4: apply RELA relocations */
    for (Elf64_Half i = 0; i < eh->e_shnum; i++) {
        const Elf64_Shdr *sh = &shdrs[i];
        if (sh->sh_type != SHT_RELA) continue;
        Elf64_Half target_sec = (Elf64_Half)sh->sh_info;
        if (target_sec >= 256 || !sh_vaddr[target_sec]) continue;

        const Elf64_Rela *relas = (const Elf64_Rela *)(elf + sh->sh_offset);
        size_t rela_count = sh->sh_size / sizeof(Elf64_Rela);

        for (size_t j = 0; j < rela_count; j++) {
            const Elf64_Rela *r = &relas[j];
            uint32_t sym_idx = (uint32_t)ELF64_R_SYM(r->r_info);
            uint32_t rtype   = (uint32_t)ELF64_R_TYPE(r->r_info);

            uint64_t S = 0; /* symbol value */
            if (sym_idx < sym_count) {
                const Elf64_Sym *sym = &symtab[sym_idx];
                uint16_t shidx = sym->st_shndx;
                if (shidx == 0) {
                    /* External symbol — look up in kernel table */
                    void *kaddr = ksym_lookup(strtab + sym->st_name);
                    if (!kaddr) { kfree(base); return -1; }
                    S = (uint64_t)(uintptr_t)kaddr;
                } else if (shidx < 256 && sh_vaddr[shidx]) {
                    S = sh_vaddr[shidx] + sym->st_value;
                }
            }

            uint8_t  *loc  = (uint8_t *)(uintptr_t)(sh_vaddr[target_sec] +
                                                      r->r_offset);
            uint64_t  P    = (uint64_t)(uintptr_t)loc;
            int64_t   A    = r->r_addend;

            switch (rtype) {
            case R_X86_64_64:
                { uint64_t v = S + (uint64_t)A;
                  memcpy(loc, &v, 8); }
                break;
            case R_X86_64_PC32:
            case R_X86_64_PLT32:
                { int32_t v = (int32_t)((int64_t)(S + (uint64_t)A - P));
                  memcpy(loc, &v, 4); }
                break;
            case R_X86_64_32:
                { uint32_t v = (uint32_t)(S + (uint64_t)A);
                  memcpy(loc, &v, 4); }
                break;
            case R_X86_64_32S:
                { int32_t v = (int32_t)((int64_t)S + A);
                  memcpy(loc, &v, 4); }
                break;
            default:
                /* Unknown relocation type */
                kfree(base); return -1;
            }
        }
    }

    return 0;
}

uint32_t adf_load(const char *path) {
    /* Read file */
    void  *elf_buf  = NULL;
    size_t elf_size = 0;
    if (read_file(path, &elf_buf, &elf_size) != 0) return 0;

    void         *load_base  = NULL;
    size_t        load_size  = 0;
    adf_driver_t *drv_entry  = NULL;
    adf_meta_t   *drv_meta   = NULL;

    if (elf_load((const uint8_t *)elf_buf, elf_size,
                 &load_base, &load_size, &drv_entry, &drv_meta) != 0) {
        kfree(elf_buf);
        return 0;
    }
    kfree(elf_buf);

    /* Allocate module slot */
    spin_lock(&g_mod_lock);
    module_t *mod = NULL;
    for (int i = 0; i < MAX_MODULES; i++) {
        if (g_modules[i].mod_id == 0) { mod = &g_modules[i]; break; }
    }
    if (!mod) { spin_unlock(&g_mod_lock); kfree(load_base); return 0; }

    uint32_t mid = g_next_mod_id++;
    memset(mod, 0, sizeof(*mod));
    mod->mod_id    = mid;
    mod->load_base = load_base;
    mod->load_size = load_size;
    memcpy(&mod->meta, drv_meta, sizeof(mod->meta));
    spin_unlock(&g_mod_lock);

    /* Initialise device context */
    adf_dev_t *dev = &mod->dev;
    dev->mod_id   = mid;
    dev->entry    = drv_entry;
    dev->state    = ADF_STATE_PROBING;
    strncpy(dev->name, drv_meta->name, ADF_NAME_LEN - 1);

    /* Probe */
    if (drv_entry->probe(dev) != 0) {
        adf_unload(mid);
        return 0;
    }

    /* Attach */
    dev->state = ADF_STATE_ACTIVE;
    if (drv_entry->attach(dev) != 0) {
        drv_entry->detach(dev);
        adf_unload(mid);
        return 0;
    }

    return mid;
}

int adf_unload(uint32_t mod_id) {
    spin_lock(&g_mod_lock);
    module_t *mod = NULL;
    for (int i = 0; i < MAX_MODULES; i++) {
        if (g_modules[i].mod_id == mod_id) { mod = &g_modules[i]; break; }
    }
    if (!mod) { spin_unlock(&g_mod_lock); return -1; }

    adf_dev_t    *dev   = &mod->dev;
    adf_driver_t *entry = dev->entry;

    dev->state = ADF_STATE_DETACHED;
    if (entry && entry->detach) entry->detach(dev);

    kfree(mod->load_base);
    mod->mod_id    = 0;
    mod->load_base = NULL;
    spin_unlock(&g_mod_lock);
    return 0;
}

adf_dev_t *adf_find(const char *name) {
    for (int i = 0; i < MAX_MODULES; i++) {
        if (g_modules[i].mod_id != 0 &&
            strncmp(g_modules[i].dev.name, name, ADF_NAME_LEN) == 0)
            return &g_modules[i].dev;
    }
    return NULL;
}

void adf_foreach(void (*cb)(adf_dev_t *dev, void *arg), void *arg) {
    for (int i = 0; i < MAX_MODULES; i++) {
        if (g_modules[i].mod_id != 0)
            cb(&g_modules[i].dev, arg);
    }
}

void adf_dispatch_irq(uint32_t vector) {
    /* IRQ routing: each module registers its vector during attach. */
    (void)vector;
    /* TODO: implement IRQ vector table */
}
