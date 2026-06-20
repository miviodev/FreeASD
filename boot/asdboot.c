/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "asdboot.h"

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef long long          i64;
typedef u64                uintn;
typedef void              *EFI_HANDLE;
typedef u64                EFI_STATUS;
typedef u16                CHAR16;

#define EFI_SUCCESS          0ULL
#define EFI_LOAD_ERROR       0x8000000000000001ULL
#define EFI_NOT_FOUND        0x800000000000000EULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_OUT_OF_RESOURCES 0x8000000000000009ULL

#define IN
#define OUT

#define EFI_ALLOCATE_ADDRESS 2U
#define EFI_LOADER_DATA      4U

typedef struct {
    u64 _reset;
    u64 _output_string;  /* EFI_STATUS(*)(this, CHAR16*) */
    u64 _pad[10];
} EFI_SIMPLE_TEXT_OUTPUT;

typedef struct {
    u64 _reset;
    u64 _read_keystroke; /* EFI_STATUS(*)(this, EFI_INPUT_KEY*) */
    u64 _wait_for_key;
} EFI_SIMPLE_TEXT_INPUT;

typedef struct {
    u16 scan_code;
    CHAR16 unicode_char;
} EFI_INPUT_KEY;

#define EFI_MEMORY_DESCRIPTOR_VERSION 1
typedef struct {
    u32 type;
    u32 _pad;
    u64 physical_start;
    u64 virtual_start;
    u64 num_pages;
    u64 attribute;
} EFI_MEMORY_DESCRIPTOR;

static u32 efi_mem_type_to_asd(u32 t) {
    switch (t) {
    /* Loader-owned RAM must not be given to the kernel buddy allocator. */
    case 1:  return ASD_MEM_LOADER;    /* EfiLoaderCode */
    case 2:  return ASD_MEM_LOADER;    /* EfiLoaderData */
    case 3:  return ASD_MEM_FREE;      /* EfiBootServicesCode (reclaimed after ExitBootServices) */
    case 4:  return ASD_MEM_FREE;      /* EfiBootServicesData */
    case 7:  return ASD_MEM_FREE;      /* EfiConventionalMemory */
    case 9:  return ASD_MEM_ACPI_REC;  /* EfiACPIReclaimMemory */
    case 10: return ASD_MEM_ACPI_NVS;  /* EfiACPIMemoryNVS */
    default: return ASD_MEM_RESERVED;
    }
}

typedef struct {
    u8   _hdr[24];
    u64  _raise_tpl;
    u64  _restore_tpl;
    u64  _allocate_pages;
    u64  _free_pages;
    EFI_STATUS (*get_memory_map)(IN OUT uintn *map_size,
                                 OUT EFI_MEMORY_DESCRIPTOR *map,
                                 OUT uintn *map_key,
                                 OUT uintn *desc_size,
                                 OUT u32   *desc_ver);
    EFI_STATUS (*allocate_pool)(u32 type, uintn size, OUT void **buf);
    EFI_STATUS (*free_pool)(void *buf);
    u64  _create_event;
    u64  _set_timer;
    u64  _wait_for_event;
    u64  _signal_event;
    u64  _close_event;
    u64  _check_event;
    EFI_STATUS (*install_protocol_interface)(void);
    EFI_STATUS (*reinstall_protocol_interface)(void);
    EFI_STATUS (*uninstall_protocol_interface)(void);
    EFI_STATUS (*handle_protocol)(EFI_HANDLE handle, const u8 *guid, void **iface);
    u64  _reserved;
    EFI_STATUS (*register_protocol_notify)(void);
    EFI_STATUS (*locate_handle)(void);
    EFI_STATUS (*locate_device_path)(void);
    EFI_STATUS (*install_configuration_table)(void);
    EFI_STATUS (*load_image)(void);
    EFI_STATUS (*start_image)(void);
    EFI_STATUS (*exit)(void);
    EFI_STATUS (*unload_image)(void);
    EFI_STATUS (*exit_boot_services)(EFI_HANDLE image, uintn map_key);
    u64  _pad3[4];
    EFI_STATUS (*stall)(uintn microseconds);
    u64  _pad4[32]; /* we don't need the rest */
} EFI_BOOT_SERVICES;

typedef struct {
    u8                      _hdr[24];
    CHAR16                 *firmware_vendor;
    u32                     firmware_revision;
    u32                     _pad;
    EFI_HANDLE              console_in_handle;
    EFI_SIMPLE_TEXT_INPUT  *con_in;
    EFI_HANDLE              console_out_handle;
    EFI_SIMPLE_TEXT_OUTPUT *con_out;
    EFI_HANDLE              std_err_handle;
    EFI_SIMPLE_TEXT_OUTPUT *std_err;
    void                   *runtime_services;
    EFI_BOOT_SERVICES      *boot_services;
    uintn                   num_table_entries;
    void                   *config_table;
} EFI_SYSTEM_TABLE;

static const u8 ACPI_20_GUID[16] = {
    0x71, 0xe8, 0x68, 0x88, 0xf1, 0xe4, 0xd3, 0x11,
    0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81
};

typedef struct {
    u8      guid[16];
    void   *table;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    u32 version;
    u32 horizontal_resolution;
    u32 vertical_resolution;
    u32 pixel_format;
    u32 pixel_information[4];
    u32 pixels_per_scan_line;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    u32 max_mode;
    u32 mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
    uintn size_of_info;
    u64 framebuffer_base;
    uintn framebuffer_size;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    EFI_STATUS (*query_mode)(void);
    EFI_STATUS (*set_mode)(void);
    EFI_STATUS (*blt)(void);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

#define EFI_GOP_PIXEL_RGBX 0
#define EFI_GOP_PIXEL_BGRX 1

static const u8 EFI_GOP_GUID[16] = {
    0xDE, 0xA9, 0x42, 0x90, 0xDC, 0x23, 0x38, 0x4A,
    0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A
};

typedef EFI_STATUS (*locate_proto_t)(const u8 *guid, void *reg, void **iface);

typedef struct {
    u8  e_ident[16];
    u16 e_type;
    u16 e_machine;
    u32 e_version;
    u64 e_entry;
    u64 e_phoff;
    u64 e_shoff;
    u32 e_flags;
    u16 e_ehsize;
    u16 e_phentsize;
    u16 e_phnum;
    u16 e_shentsize;
    u16 e_shnum;
    u16 e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    u32 p_type;
    u32 p_flags;
    u64 p_offset;
    u64 p_vaddr;
    u64 p_paddr;
    u64 p_filesz;
    u64 p_memsz;
    u64 p_align;
} elf64_phdr_t;

#define ELF_PT_LOAD 1U

static EFI_SYSTEM_TABLE  *g_st;
static EFI_BOOT_SERVICES *g_bs;
static EFI_HANDLE         g_image;
static u64                g_fb_phys;
static u32                g_fb_width;
static u32                g_fb_height;
static u32                g_fb_stride;
static u8                 g_fb_format;

typedef EFI_STATUS (*alloc_pages_fn_t)(u32 alloc_type, u32 mem_type,
                                       uintn pages, u64 *memory);

typedef EFI_STATUS (*output_fn_t)(EFI_SIMPLE_TEXT_OUTPUT *, CHAR16 *);

static void
con_puts(const CHAR16 *s)
{
    output_fn_t fn = (output_fn_t)(void *)g_st->con_out->_output_string;
    fn(g_st->con_out, (CHAR16 *)s);
}

static void
con_puts_a(const char *s)
{
    CHAR16 buf[256];
    int i = 0;
    while (*s && i < 254) {
        buf[i++] = (CHAR16)(unsigned char)*s++;
    }
    buf[i] = 0;
    con_puts(buf);
}

static void
con_putu(u64 n)
{
    char buf[22];
    int i = 21;
    buf[i] = '\0';
    if (n == 0) { buf[--i] = '0'; }
    while (n) { buf[--i] = (char)('0' + n % 10); n /= 10; }
    con_puts_a(&buf[i]);
}

static void
con_putc(char c)
{
    char s[2] = { c, 0 };
    con_puts_a(s);
}

static void
build_asd_mmap(asd_mmap_entry_t *asd_mmap, EFI_MEMORY_DESCRIPTOR *mmap,
               uintn map_sz, uintn desc_sz, asd_bib_t *bib)
{
    uintn entry_count = map_sz / desc_sz;

    for (uintn i = 0; i < entry_count; i++) {
        EFI_MEMORY_DESCRIPTOR *e =
            (EFI_MEMORY_DESCRIPTOR *)((u8 *)mmap + i * desc_sz);
        asd_mmap[i].base  = e->physical_start;
        asd_mmap[i].pages = e->num_pages;
        asd_mmap[i].type  = efi_mem_type_to_asd(e->type);
        asd_mmap[i]._pad  = 0;
    }

    if (bib) {
        bib->mmap_phys     = (u64)(uintn)asd_mmap;
        bib->mmap_count    = (u32)entry_count;
        bib->mmap_entry_sz = (u32)sizeof(asd_mmap_entry_t);
    }
}

static void
mark_asd_range(asd_mmap_entry_t *asd_mmap, uintn entry_count,
               u64 base, u64 size, u32 type)
{
    if (!base || size == 0) return;
    u64 lo = base & ~0xFFFULL;
    u64 hi = (base + size + 0xFFFULL) & ~0xFFFULL;
    for (uintn i = 0; i < entry_count; i++) {
        u64 rlo = asd_mmap[i].base;
        u64 rhi = asd_mmap[i].base + asd_mmap[i].pages * 4096ULL;
        if (hi <= rlo || lo >= rhi) continue;
        /* Coarse marking: if any overlap, mark entire region. */
        asd_mmap[i].type = type;
    }
}

/* Must run after every build_asd_mmap() — rebuild wipes manual marks. */
static void
mark_boot_reserved(asd_mmap_entry_t *asd_mmap, uintn entry_count, asd_bib_t *bib,
                  u64 kernel_phys_lo, u64 kernel_phys_hi,
                  void *kernel_buf, uintn kernel_sz,
                  void *initrd_buf, uintn initrd_sz,
                  void *cmdline_buf, u64 cmdline_len)
{
    mark_asd_range(asd_mmap, entry_count, (u64)(uintn)bib, sizeof(*bib), ASD_MEM_LOADER);
    mark_asd_range(asd_mmap, entry_count, (u64)(uintn)asd_mmap,
                   (u64)(sizeof(asd_mmap_entry_t) * entry_count), ASD_MEM_LOADER);
    if (cmdline_buf && cmdline_len)
        mark_asd_range(asd_mmap, entry_count, (u64)(uintn)cmdline_buf,
                       cmdline_len + 1, ASD_MEM_LOADER);
    if (initrd_buf && initrd_sz)
        mark_asd_range(asd_mmap, entry_count, (u64)(uintn)initrd_buf,
                       initrd_sz, ASD_MEM_INITRD);
    if (kernel_phys_lo && kernel_phys_hi && kernel_phys_hi > kernel_phys_lo) {
        mark_asd_range(asd_mmap, entry_count, kernel_phys_lo,
                       kernel_phys_hi - kernel_phys_lo, ASD_MEM_KERNEL);
    } else if (kernel_buf && kernel_sz) {
        mark_asd_range(asd_mmap, entry_count, (u64)(uintn)kernel_buf,
                       kernel_sz, ASD_MEM_KERNEL);
    }
    if (bib && bib->fb_phys && bib->fb_stride && bib->fb_height) {
        u64 fb_sz = (u64)bib->fb_stride * (u64)bib->fb_height;
        mark_asd_range(asd_mmap, entry_count, bib->fb_phys, fb_sz, ASD_MEM_FB);
    }
}

static void *
mem_set(void *dst, int c, uintn n)
{
    u8 *p = dst;
    while (n--) *p++ = (u8)c;
    return dst;
}

static void *
mem_cpy(void *dst, const void *src, uintn n)
{
    u8 *d = dst;
    const u8 *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *
memcpy(void *dst, const void *src, uintn n)
{
    return mem_cpy(dst, src, n);
}

void *
memset(void *dst, int c, uintn n)
{
    return mem_set(dst, c, n);
}

static int
str_eq(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int
str_len(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

static void
str_cpy(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int
is_digit(char c) { return c >= '0' && c <= '9'; }
static int
is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

#define ABC_MAX_ENTRIES  8
#define ABC_MAX_STR      256

typedef struct {
    char id[ABC_MAX_STR];
    char label[ABC_MAX_STR];
    char kernel[ABC_MAX_STR];
    char initrd[ABC_MAX_STR];
    char cmdline[ABC_MAX_STR];
    char uki[ABC_MAX_STR];
    int  is_uki;       /* 1 if uki= is set, 0 if kernel= is set */
} abc_entry_t;

typedef struct {
    int         timeout;
    char        default_id[ABC_MAX_STR];
    char        menu_title[ABC_MAX_STR];
    int         entry_count;
    abc_entry_t entries[ABC_MAX_ENTRIES];
} abc_config_t;

typedef struct {
    const char *src;
    int         pos;
    int         len;
    int         line;
} abc_parser_t;

static void abc_skip_ws(abc_parser_t *p) {
    while (p->pos < p->len && is_space(p->src[p->pos]))
        p->pos++;
}

static int abc_at_end(abc_parser_t *p) {
    return p->pos >= p->len;
}

static int
abc_read_line(abc_parser_t *p, char *out, int max)
{
    int start = p->pos;
    /* skip leading spaces */
    while (p->pos < p->len && (p->src[p->pos] == ' ' || p->src[p->pos] == '\t'))
        p->pos++;
    int wstart = p->pos;
    while (p->pos < p->len && p->src[p->pos] != '\n') p->pos++;
    int end = p->pos;
    if (p->pos < p->len) { p->pos++; p->line++; } /* consume '\n' */
    (void)start;
    /* trim trailing whitespace */
    while (end > wstart && is_space(p->src[end - 1])) end--;
    int n = end - wstart;
    if (n >= max) n = max - 1;
    mem_cpy(out, p->src + wstart, (uintn)n);
    out[n] = '\0';
    return n;
}

static int
abc_split_kv(const char *line, char *key, char *val, int max)
{
    const char *eq = line;
    while (*eq && *eq != '=') eq++;
    if (!*eq) return 0;
    int klen = (int)(eq - line);
    while (klen > 0 && is_space(line[klen - 1])) klen--;
    if (klen <= 0 || klen >= max) return 0;
    mem_cpy(key, line, (uintn)klen); key[klen] = '\0';
    const char *v = eq + 1;
    while (*v && is_space(*v)) v++;
    /* strip surrounding quotes if present */
    int vlen = str_len(v);
    if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"') {
        v++; vlen -= 2;
    }
    if (vlen >= max) vlen = max - 1;
    mem_cpy(val, v, (uintn)vlen); val[vlen] = '\0';
    return 1;
}

static int
abc_parse(const char *text, int textlen, abc_config_t *cfg)
{
    abc_parser_t p;
    p.src  = text;
    p.pos  = 0;
    p.len  = textlen;
    p.line = 1;

    mem_set(cfg, 0, sizeof(*cfg));
    cfg->timeout = 3;
    str_cpy(cfg->menu_title, "ASD Boot", ABC_MAX_STR);

    int in_menu  = 0;
    int in_entry = 0;
    abc_entry_t cur;
    mem_set(&cur, 0, sizeof(cur));

    char line[ABC_MAX_STR];
    char key[ABC_MAX_STR];
    char val[ABC_MAX_STR];

    while (!abc_at_end(&p)) {
        abc_read_line(&p, line, sizeof(line));
        if (line[0] == '#' || line[0] == '\0') continue;

        if (str_eq(line, "end")) {
            if (in_entry) {
                if (cfg->entry_count < ABC_MAX_ENTRIES)
                    cfg->entries[cfg->entry_count++] = cur;
                mem_set(&cur, 0, sizeof(cur));
            }
            in_menu  = 0;
            in_entry = 0;
            continue;
        }

        if (str_eq(line, "menu"))  { in_menu  = 1; continue; }
        if (str_eq(line, "entry")) { in_entry = 1; continue; }

        if (!abc_split_kv(line, key, val, ABC_MAX_STR)) continue;

        if (!in_menu && !in_entry) {
            /* top-level keys */
            if (str_eq(key, "timeout")) {
                cfg->timeout = 0;
                for (int i = 0; val[i]; i++)
                    if (is_digit(val[i]))
                        cfg->timeout = cfg->timeout * 10 + (val[i] - '0');
            }
        } else if (in_menu) {
            if (str_eq(key, "title"))   str_cpy(cfg->menu_title, val, ABC_MAX_STR);
            if (str_eq(key, "default")) str_cpy(cfg->default_id, val, ABC_MAX_STR);
        } else if (in_entry) {
            if (str_eq(key, "id"))      str_cpy(cur.id,      val, ABC_MAX_STR);
            if (str_eq(key, "label"))   str_cpy(cur.label,   val, ABC_MAX_STR);
            if (str_eq(key, "kernel"))  str_cpy(cur.kernel,  val, ABC_MAX_STR);
            if (str_eq(key, "initrd"))  str_cpy(cur.initrd,  val, ABC_MAX_STR);
            if (str_eq(key, "cmdline")) str_cpy(cur.cmdline, val, ABC_MAX_STR);
            if (str_eq(key, "uki")) {
                str_cpy(cur.uki, val, ABC_MAX_STR);
                cur.is_uki = 1;
            }
        }
    }
    return 1;
}

#define BOX_H  u"\x2500"   /* ─ */
#define BOX_V  u"\x2502"   /* │ */
#define BOX_TL u"\x250C"   /* ┌ */
#define BOX_TR u"\x2510"   /* ┐ */
#define BOX_BL u"\x2514"   /* └ */
#define BOX_BR u"\x2518"   /* ┘ */
#define BOX_LM u"\x251C"   /* ├ */
#define BOX_RM u"\x2524"   /* ┤ */

#define MENU_WIDTH  42

static void
draw_hline(const CHAR16 *left, const CHAR16 *right)
{
    con_puts(left);
    for (int i = 0; i < MENU_WIDTH - 2; i++) con_puts(BOX_H);
    con_puts(right);
    con_puts(u"\r\n");
}

static void
draw_row(const char *text, int selected, int padlen)
{
    con_puts(BOX_V);
    if (selected) con_puts(u"  > ");
    else          con_puts(u"    ");
    con_puts_a(text);
    int used = 4 + str_len(text);
    for (int i = used; i < padlen; i++) con_puts(u" ");
    con_puts(BOX_V);
    con_puts(u"\r\n");
}

static int
draw_menu(abc_config_t *cfg, int selected, int countdown)
{
    /* Clear screen */
    output_fn_t cls = (output_fn_t)(void *)g_st->con_out->_output_string;
    cls(g_st->con_out, u"\x1B[2J\x1B[H");

    draw_hline(BOX_TL, BOX_TR);

    /* Title row */
    {
        char title[ABC_MAX_STR];
        str_cpy(title, cfg->menu_title, ABC_MAX_STR);
        int tlen = str_len(title);
        int pad  = (MENU_WIDTH - 2 - tlen) / 2;
        con_puts(BOX_V);
        for (int i = 0; i < pad; i++) con_puts(u" ");
        con_puts_a(title);
        for (int i = pad + tlen; i < MENU_WIDTH - 2; i++) con_puts(u" ");
        con_puts(BOX_V);
        con_puts(u"\r\n");
    }

    draw_hline(BOX_LM, BOX_RM);

    /* Entry rows */
    for (int i = 0; i < cfg->entry_count; i++)
        draw_row(cfg->entries[i].label, i == selected, MENU_WIDTH - 2);

    draw_hline(BOX_LM, BOX_RM);

    /* Help row */
    draw_row("[up/down] select  [Enter] boot", 0, MENU_WIDTH - 2);
    draw_row("[e] edit cmdline  [Esc] firmware", 0, MENU_WIDTH - 2);

    draw_hline(BOX_LM, BOX_RM);

    /* Countdown row */
    if (countdown > 0) {
        con_puts(BOX_V);
        con_puts(u"  Booting in ");
        con_putu((u64)countdown);
        con_puts(u" second");
        if (countdown != 1) con_puts(u"s");
        con_puts(u"...                   ");
        con_puts(BOX_V);
        con_puts(u"\r\n");
    } else {
        draw_row("", 0, MENU_WIDTH - 2);
    }

    draw_hline(BOX_BL, BOX_BR);
    return 0;
}

#define KEY_UP    0x01
#define KEY_DOWN  0x02
#define KEY_ENTER 0x03
#define KEY_ESC   0x04
#define KEY_E     0x05
#define KEY_NONE  0x00

static int
read_key(void)
{
    EFI_INPUT_KEY k;
    typedef EFI_STATUS (*read_fn_t)(EFI_SIMPLE_TEXT_INPUT *, EFI_INPUT_KEY *);
    read_fn_t rfn = (read_fn_t)(void *)g_st->con_in->_read_keystroke;
    EFI_STATUS st = rfn(g_st->con_in, &k);
    if (st != EFI_SUCCESS) return KEY_NONE;
    if (k.scan_code == 0x01) return KEY_UP;
    if (k.scan_code == 0x02) return KEY_DOWN;
    if (k.scan_code == 0x17) return KEY_ESC;
    if (k.unicode_char == 0x0D) return KEY_ENTER;
    if (k.unicode_char == 'e' || k.unicode_char == 'E') return KEY_E;
    return KEY_NONE;
}

typedef struct EFI_FILE_PROTOCOL EFI_FILE;
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM;

struct EFI_FILE_PROTOCOL {
    u64 revision;
    EFI_STATUS (*open)(EFI_FILE *f, EFI_FILE **new_h, CHAR16 *name, u64 mode, u64 attr);
    EFI_STATUS (*close)(EFI_FILE *f);
    EFI_STATUS (*delete_)(EFI_FILE *f);
    EFI_STATUS (*read)(EFI_FILE *f, uintn *sz, void *buf);
    EFI_STATUS (*write)(EFI_FILE *f, uintn *sz, void *buf);
    EFI_STATUS (*get_position)(EFI_FILE *f, u64 *pos);
    EFI_STATUS (*set_position)(EFI_FILE *f, u64 pos);
    EFI_STATUS (*get_info)(EFI_FILE *f, const u8 *type, uintn *buf_sz, void *buf);
    EFI_STATUS (*set_info)(EFI_FILE *f, const u8 *type, uintn buf_sz, void *buf);
    EFI_STATUS (*flush)(EFI_FILE *f);
};

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    u64 revision;
    EFI_STATUS (*open_volume)(EFI_SIMPLE_FILE_SYSTEM *sfs, EFI_FILE **root);
};

typedef struct {
    u32        revision;
    EFI_HANDLE parent_handle;
    EFI_SYSTEM_TABLE *system_table;
    EFI_HANDLE device_handle;
    void      *file_path;
    void      *reserved;
    u32        load_options_size;
    void      *load_options;
    void      *image_base;
    u64        image_size;
    u32        image_code_type;
    u32        image_data_type;
    u64        unload;
} EFI_LOADED_IMAGE_PROTOCOL;

static const u8 EFI_SFS_GUID[16] = {
    0x22, 0x5B, 0x4E, 0x96, 0x59, 0x64, 0xD2, 0x11,
    0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
};

static const u8 EFI_LOADED_IMAGE_GUID[16] = {
    0xA1, 0x31, 0x1B, 0x5B, 0x62, 0x95, 0xD2, 0x11,
    0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
};

static EFI_STATUS
efi_read_file(const char *path_ascii, OUT void **buf_out, OUT uintn *size_out)
{
    /* Resolve filesystem from the current boot image handle. */
    EFI_LOADED_IMAGE_PROTOCOL *li = (void *)0;
    void *sfs = (void *)0;

    EFI_STATUS st = g_bs->handle_protocol(g_image, EFI_LOADED_IMAGE_GUID, (void **)&li);
    if (st != EFI_SUCCESS || !li) {
        con_puts_a("asdboot: hp(image) failed ");
        con_putu((u64)st);
        con_puts(u"\r\n");
        return st;
    }

    st = g_bs->handle_protocol(li->device_handle, EFI_SFS_GUID, &sfs);
    if (st != EFI_SUCCESS) {
        con_puts_a("asdboot: hp(sfs) failed ");
        con_putu((u64)st);
        con_puts(u"\r\n");
        return st;
    }

    EFI_FILE *root = (void *)0;
    st = ((EFI_SIMPLE_FILE_SYSTEM *)sfs)->open_volume((EFI_SIMPLE_FILE_SYSTEM *)sfs, &root);
    if (st != EFI_SUCCESS) {
        con_puts_a("asdboot: open_volume failed ");
        con_putu((u64)st);
        con_puts(u"\r\n");
        return st;
    }

    /* Convert ASCII path to CHAR16 */
    CHAR16 wpath[512];
    int i = 0;
    /* Replace '/' with '\' for UEFI */
    const char *s = path_ascii;
    while (*s == '/' || *s == '\\') s++;
    while (*s && i < 510) {
        wpath[i++] = (*s == '/') ? (CHAR16)'\\' : (CHAR16)(unsigned char)*s;
        s++;
    }
    wpath[i] = 0;

    EFI_FILE *fh = (void *)0;
    st = root->open(root, &fh, wpath, 1 /*EFI_FILE_MODE_READ*/, 0);
    if (st != EFI_SUCCESS) {
        con_puts_a("asdboot: open failed ");
        con_putu((u64)st);
        con_puts_a(" path=");
        con_puts_a(path_ascii);
        con_puts(u"\r\n");
        return st;
    }

    static const u8 EFI_FILE_INFO_GUID[16] = {
        0x92, 0x6E, 0x57, 0x09, 0x3F, 0x6D, 0xD2, 0x11,
        0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B
    };
    u8 info_buf[128];
    uintn info_sz = sizeof(info_buf);
    st = fh->get_info(fh, EFI_FILE_INFO_GUID, &info_sz, info_buf);
    if (st != EFI_SUCCESS) {
        con_puts_a("asdboot: get_info failed ");
        con_putu((u64)st);
        con_puts(u"\r\n");
        return st;
    }
    u64 file_size = *((u64 *)(info_buf + 8)); /* EFI_FILE_INFO.FileSize at offset 8 */

    /* Allocate pool for file content */
    void *fbuf = (void *)0;
    st = g_bs->allocate_pool(2 /*EfiRuntimeServicesData*/, file_size + 1, &fbuf);
    if (st != EFI_SUCCESS) return st;

    uintn read_sz = (uintn)file_size;
    st = fh->read(fh, &read_sz, fbuf);
    if (st != EFI_SUCCESS) {
        con_puts_a("asdboot: read failed ");
        con_putu((u64)st);
        con_puts(u"\r\n");
        g_bs->free_pool(fbuf);
        return st;
    }

    ((u8 *)fbuf)[read_sz] = 0; /* null terminate for text files */
    *buf_out  = fbuf;
    *size_out = read_sz;

    fh->close(fh);
    root->close(root);
    return EFI_SUCCESS;
}

static EFI_STATUS
build_and_handoff(abc_entry_t *entry)
{
    con_puts(u"asdboot: handoff begin\r\n");
    /* Step 1: load kernel (or UKI) and optional initrd */
    void  *kernel_buf = (void *)0;
    uintn  kernel_sz  = 0;
    void  *kernel_entry_ptr = (void *)0;
    u64    kernel_phys_lo = 0;
    u64    kernel_phys_hi = 0;
    void  *initrd_buf = (void *)0;
    uintn  initrd_sz  = 0;
    void  *cmdline_buf = (void *)0;

    if (entry->is_uki) {
        /* UKI: load the EFI binary; it is self-contained */
        EFI_STATUS st = efi_read_file(entry->uki, &kernel_buf, &kernel_sz);
        if (st != EFI_SUCCESS) {
            con_puts_a("asdboot: cannot load UKI: ");
            con_puts_a(entry->uki);
            con_puts(u"\r\n");
            return st;
        }
        con_puts(u"asdboot: kernel loaded\r\n");
    } else {
        EFI_STATUS st = efi_read_file(entry->kernel, &kernel_buf, &kernel_sz);
        if (st != EFI_SUCCESS && str_eq(entry->kernel, "/boot/asdkernel.bin")) {
            static const char *fallbacks[] = {
                "/asdkernel.bin",
                "/EFI/BOOT/asdkernel.bin",
                "boot/asdkernel.bin",
                "asdkernel.bin",
                "EFI/BOOT/asdkernel.bin",
                0
            };
            for (int i = 0; fallbacks[i]; i++) {
                st = efi_read_file(fallbacks[i], &kernel_buf, &kernel_sz);
                if (st == EFI_SUCCESS) break;
            }
        }
        if (st != EFI_SUCCESS) {
            con_puts_a("asdboot: cannot load kernel: ");
            con_puts_a(entry->kernel);
            con_puts(u"\r\n");
            return st;
        }

        /* If kernel is ELF64, materialize PT_LOAD segments at linked p_paddr. */
        if (kernel_sz >= sizeof(elf64_ehdr_t)) {
            elf64_ehdr_t *eh = (elf64_ehdr_t *)kernel_buf;
            if (eh->e_ident[0] == 0x7f && eh->e_ident[1] == 'E' &&
                eh->e_ident[2] == 'L' && eh->e_ident[3] == 'F' &&
                eh->e_ident[4] == 2 && eh->e_phentsize == sizeof(elf64_phdr_t) &&
                eh->e_phoff + (u64)eh->e_phnum * sizeof(elf64_phdr_t) <= (u64)kernel_sz) {
                elf64_phdr_t *ph = (elf64_phdr_t *)((u8 *)kernel_buf + eh->e_phoff);
                alloc_pages_fn_t alloc_pages =
                    (alloc_pages_fn_t)(void *)(uintn)g_bs->_allocate_pages;
                u64 low = ~0ULL;
                u64 high = 0;
                for (u16 i = 0; i < eh->e_phnum; i++) {
                    if (ph[i].p_type != ELF_PT_LOAD || ph[i].p_memsz == 0) {
                        continue;
                    }
                    if (ph[i].p_paddr < 0x1000000ULL) {
                        continue; /* skip low metadata/header LOAD segments */
                    }
                    if (ph[i].p_offset + ph[i].p_filesz > (u64)kernel_sz) return EFI_LOAD_ERROR;
                    u64 seg_lo = ph[i].p_paddr & ~0xFFFULL;
                    u64 seg_hi = (ph[i].p_paddr + ph[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
                    if (seg_lo < low) low = seg_lo;
                    if (seg_hi > high) high = seg_hi;
                }
                if (low != ~0ULL && high > low) {
                    u64 alloc_addr = low;
                    uintn pages = (uintn)((high - low) >> 12);
                    st = alloc_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, pages, &alloc_addr);
                    if (st != EFI_SUCCESS) return st;
                    mem_set((void *)(uintn)low, 0, (uintn)(high - low));

                    for (u16 i = 0; i < eh->e_phnum; i++) {
                        if (ph[i].p_type != ELF_PT_LOAD || ph[i].p_memsz == 0) continue;
                        if (ph[i].p_paddr < 0x1000000ULL) continue;
                        void *dst = (void *)(uintn)ph[i].p_paddr;
                        const void *src = (u8 *)kernel_buf + (uintn)ph[i].p_offset;
                        mem_cpy(dst, src, (uintn)ph[i].p_filesz);
                    }

                    kernel_entry_ptr = (void *)(uintn)eh->e_entry;
                    kernel_phys_lo = low;
                    kernel_phys_hi = high;
                }
            }
        }
        if (entry->initrd[0]) {
            st = efi_read_file(entry->initrd, &initrd_buf, &initrd_sz);
            if (st != EFI_SUCCESS) {
                con_puts_a("asdboot: cannot load initrd: ");
                con_puts_a(entry->initrd);
                con_puts(u"\r\n");
                return st;
            }
        }
    }

    /* Step 2: copy cmdline */
    int clen = str_len(entry->cmdline);
    g_bs->allocate_pool(2, (uintn)(clen + 1), &cmdline_buf);
    mem_cpy(cmdline_buf, entry->cmdline, (uintn)(clen + 1));
    con_puts(u"asdboot: cmdline ready\r\n");

    /* Step 3: get memory map & exit boot services */
    uintn map_sz   = 0;
    uintn map_key  = 0;
    uintn desc_sz  = 0;
    u32   desc_ver = 0;
    EFI_MEMORY_DESCRIPTOR *mmap = (void *)0;

    /* First call to get required size */
    EFI_STATUS st = g_bs->get_memory_map(&map_sz, (void *)0, &map_key,
                                          &desc_sz, &desc_ver);
    map_sz += desc_sz * 8; /* add slack for allocations below */
    g_bs->allocate_pool(2, map_sz, (void **)&mmap);
    st = g_bs->get_memory_map(&map_sz, mmap, &map_key, &desc_sz, &desc_ver);
    if (st != EFI_SUCCESS) {
        con_puts(u"asdboot: get_memory_map failed\r\n");
        return st;
    }
    con_puts(u"asdboot: memory map ready\r\n");

    /* Step 4: build BIB */
    asd_bib_t *bib = (void *)0;
    g_bs->allocate_pool(2, sizeof(asd_bib_t), (void **)&bib);
    mem_set(bib, 0, sizeof(asd_bib_t));

    /* Build ASD memory map array */
    uintn entry_count = map_sz / desc_sz;
    asd_mmap_entry_t *asd_mmap = (void *)0;
    g_bs->allocate_pool(2, sizeof(asd_mmap_entry_t) * entry_count,
                        (void **)&asd_mmap);
    build_asd_mmap(asd_mmap, mmap, map_sz, desc_sz, (asd_bib_t *)0);

    /* Find ACPI RSDP */
    u64 rsdp = 0;
    EFI_CONFIGURATION_TABLE *ct =
        (EFI_CONFIGURATION_TABLE *)g_st->config_table;
    for (uintn i = 0; i < g_st->num_table_entries; i++) {
        u8 match = 1;
        for (int j = 0; j < 16; j++)
            if (ct[i].guid[j] != ACPI_20_GUID[j]) { match = 0; break; }
        if (match) { rsdp = (u64)(uintn)ct[i].table; break; }
    }

    bib->magic         = ASD_BIB_MAGIC;
    bib->version       = ASD_BIB_VERSION;
    bib->size          = (u16)sizeof(asd_bib_t);
    bib->mmap_phys     = (u64)(uintn)asd_mmap;
    bib->mmap_count    = (u32)entry_count;
    bib->mmap_entry_sz = (u32)sizeof(asd_mmap_entry_t);
    bib->fb_phys       = g_fb_phys;
    bib->fb_width      = g_fb_width;
    bib->fb_height     = g_fb_height;
    bib->fb_stride     = g_fb_stride;
    bib->fb_format     = g_fb_format;
    bib->acpi_rsdp_phys = rsdp;
    bib->cmdline_phys  = (u64)(uintn)cmdline_buf;
    bib->cmdline_len   = (u32)clen;
    bib->initrd_phys   = (u64)(uintn)initrd_buf;
    bib->initrd_len    = (u64)initrd_sz;
    con_puts(u"asdboot: bib ready\r\n");

    for (int tries = 0; tries < 4; tries++) {
        uintn cur_sz = map_sz;
        st = g_bs->get_memory_map(&cur_sz, mmap, &map_key, &desc_sz, &desc_ver);
        if (st == EFI_SUCCESS) {
            map_sz = cur_sz;
            break;
        }
        if (st != EFI_BUFFER_TOO_SMALL) return st;

        g_bs->free_pool(mmap);
        map_sz = cur_sz + desc_sz * 8 + 4096;
        st = g_bs->allocate_pool(2, map_sz, (void **)&mmap);
        if (st != EFI_SUCCESS) return st;
    }
    if (st != EFI_SUCCESS) return st;
    build_asd_mmap(asd_mmap, mmap, map_sz, desc_sz, bib);
    mark_boot_reserved(asd_mmap, entry_count, bib,
                       kernel_phys_lo, kernel_phys_hi,
                       kernel_buf, kernel_sz,
                       initrd_buf, initrd_sz,
                       cmdline_buf, (u64)clen);
    con_puts(u"asdboot: exiting boot services\r\n");

    st = g_bs->exit_boot_services(g_image, map_key);
    if (st != EFI_SUCCESS) {
        /* Retry once with a fresh map */
        for (int tries = 0; tries < 4; tries++) {
            uintn cur_sz = map_sz;
            st = g_bs->get_memory_map(&cur_sz, mmap, &map_key, &desc_sz, &desc_ver);
            if (st == EFI_SUCCESS) {
                map_sz = cur_sz;
                break;
            }
            if (st != EFI_BUFFER_TOO_SMALL) return st;

            g_bs->free_pool(mmap);
            map_sz = cur_sz + desc_sz * 8 + 4096;
            st = g_bs->allocate_pool(2, map_sz, (void **)&mmap);
            if (st != EFI_SUCCESS) return st;
        }
        if (st != EFI_SUCCESS) return st;
        build_asd_mmap(asd_mmap, mmap, map_sz, desc_sz, bib);
        mark_boot_reserved(asd_mmap, entry_count, bib,
                           kernel_phys_lo, kernel_phys_hi,
                           kernel_buf, kernel_sz,
                           initrd_buf, initrd_sz,
                           cmdline_buf, (u64)clen);
        st = g_bs->exit_boot_services(g_image, map_key);
        if (st != EFI_SUCCESS) {
            con_puts_a("asdboot: exit_boot_services failed ");
            con_putu((u64)st);
            con_puts(u"\r\n");
            return st;
        }
    }

    {
        void *entry_addr = kernel_entry_ptr ? kernel_entry_ptr : kernel_buf;
        __asm__ volatile(
            "mov %0, %%rdi\n\t"
            "mov %1, %%rsi\n\t"
            "xor %%rbp, %%rbp\n\t"
            "jmp *%2\n\t"
            :
            : "r"(bib), "r"((u64)ASD_BOOT_MAGIC), "r"(entry_addr)
            : "rdi", "rsi", "rbp", "memory"
        );
    }
}

static void
fallback_prompt(abc_config_t *cfg)
{
    /* Reuse last entry as template — user types a kernel path */
    con_puts(u"asdboot: enter kernel path (or 'halt'):\r\n> ");
    /* Read a line from console input */
    char path[256];
    int  i = 0;
    while (i < 255) {
        EFI_INPUT_KEY k;
        typedef EFI_STATUS (*read_fn_t)(EFI_SIMPLE_TEXT_INPUT *, EFI_INPUT_KEY *);
        read_fn_t rfn = (read_fn_t)(void *)g_st->con_in->_read_keystroke;
        /* Busy-wait */
        EFI_STATUS st;
        do { st = rfn(g_st->con_in, &k); } while (st != EFI_SUCCESS);
        if (k.unicode_char == 0x0D) break;
        if (k.unicode_char == 0x08 && i > 0) { i--; con_puts(u"\b \b"); continue; }
        path[i++] = (char)k.unicode_char;
        char ec[2] = { (char)k.unicode_char, 0 };
        con_puts_a(ec);
    }
    path[i] = '\0';
    con_puts(u"\r\n");
    if (str_eq(path, "halt")) {
        con_puts(u"asdboot: halting.\r\n");
        for (;;) __asm__ volatile("hlt");
    }
    /* Attempt to boot with the typed path */
    if (cfg->entry_count == 0) {
        /* Create a synthetic entry */
        cfg->entry_count = 1;
    }
    str_cpy(cfg->entries[0].kernel, path, ABC_MAX_STR);
    cfg->entries[0].is_uki = 0;
    build_and_handoff(&cfg->entries[0]);
    con_puts(u"asdboot: handoff failed. Halting.\r\n");
    for (;;) __asm__ volatile("hlt");
}

EFI_STATUS
efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *systab)
{
    g_st = systab;
    g_bs = systab->boot_services;
    g_image = image_handle;
    g_fb_phys = 0;
    g_fb_width = 0;
    g_fb_height = 0;
    g_fb_stride = 0;
    g_fb_format = ASD_FB_NONE;

    {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = (void *)0;
        EFI_STATUS gop_st =
            g_bs->handle_protocol(g_st->console_out_handle, EFI_GOP_GUID, (void **)&gop);
        if (gop_st != EFI_SUCCESS) {
            u64 *bs_ptrs = (u64 *)(void *)g_bs;
            locate_proto_t locate = (locate_proto_t)(void *)bs_ptrs[40];
            if (locate) gop_st = locate(EFI_GOP_GUID, (void *)0, (void **)&gop);
        }
        if (gop_st == EFI_SUCCESS && gop && gop->mode && gop->mode->info) {
            g_fb_phys = gop->mode->framebuffer_base;
            g_fb_width = gop->mode->info->horizontal_resolution;
            g_fb_height = gop->mode->info->vertical_resolution;
            g_fb_stride = gop->mode->info->pixels_per_scan_line;
            if (gop->mode->info->pixel_format == EFI_GOP_PIXEL_RGBX) g_fb_format = ASD_FB_RGBX32;
            else if (gop->mode->info->pixel_format == EFI_GOP_PIXEL_BGRX) g_fb_format = ASD_FB_BGRX32;

        }
    }

    con_puts(u"asdboot v1.0\r\n");

    /* Load asdboot config with fallbacks across common FAT layouts. */
    void  *conf_buf = (void *)0;
    uintn  conf_sz  = 0;
    EFI_STATUS st = efi_read_file("/boot/asdboot.conf", &conf_buf, &conf_sz);
    if (st != EFI_SUCCESS) {
        st = efi_read_file("/asdboot.conf", &conf_buf, &conf_sz);
    }
    if (st != EFI_SUCCESS) {
        st = efi_read_file("/EFI/BOOT/asdboot.conf", &conf_buf, &conf_sz);
    }
    if (st != EFI_SUCCESS) {
        st = efi_read_file("boot/asdboot.conf", &conf_buf, &conf_sz);
    }
    if (st != EFI_SUCCESS) {
        st = efi_read_file("asdboot.conf", &conf_buf, &conf_sz);
    }
    if (st != EFI_SUCCESS) {
        st = efi_read_file("EFI/BOOT/asdboot.conf", &conf_buf, &conf_sz);
    }
    if (st != EFI_SUCCESS) {
        con_puts(u"asdboot: cannot open config, using default entry\r\n");

        abc_config_t cfg;
        mem_set(&cfg, 0, sizeof(cfg));
        cfg.timeout = 0;
        cfg.entry_count = 1;
        str_cpy(cfg.menu_title, "ASD Boot", ABC_MAX_STR);
        str_cpy(cfg.entries[0].id, "default", ABC_MAX_STR);
        str_cpy(cfg.entries[0].label, "ASD Kernel", ABC_MAX_STR);
        str_cpy(cfg.entries[0].kernel, "asdkernel.bin", ABC_MAX_STR);
        str_cpy(cfg.entries[0].cmdline, "", ABC_MAX_STR);
        cfg.entries[0].is_uki = 0;

        st = build_and_handoff(&cfg.entries[0]);
        if (st != EFI_SUCCESS) {
            con_puts(u"asdboot: default boot failed\r\n");
            fallback_prompt(&cfg);
        }
        return EFI_LOAD_ERROR;
    }

    abc_config_t cfg;
    if (!abc_parse((const char *)conf_buf, (int)conf_sz, &cfg)) {
        con_puts(u"asdboot: config parse error\r\n");
        fallback_prompt(&cfg);
        return EFI_LOAD_ERROR;
    }
    g_bs->free_pool(conf_buf);

    if (cfg.entry_count == 0) {
        con_puts(u"asdboot: no boot entries found\r\n");
        fallback_prompt(&cfg);
        return EFI_LOAD_ERROR;
    }

    /* Find default entry index */
    int def_idx = 0;
    if (cfg.default_id[0]) {
        for (int i = 0; i < cfg.entry_count; i++) {
            if (str_eq(cfg.entries[i].id, cfg.default_id)) {
                def_idx = i; break;
            }
        }
    }

    /* If only one entry and timeout == 0, skip menu */
    if (cfg.entry_count == 1 && cfg.timeout == 0) {
        con_puts(u"asdboot: direct boot path\r\n");
        st = build_and_handoff(&cfg.entries[0]);
        if (st != EFI_SUCCESS) {
            con_puts_a("asdboot: handoff failed ");
            con_putu((u64)st);
            con_puts(u"\r\n");
        }
        fallback_prompt(&cfg);
        return EFI_LOAD_ERROR;
    }

    /* Interactive menu with countdown */
    int selected  = def_idx;
    int countdown = cfg.timeout;
    int user_acted = 0;

    draw_menu(&cfg, selected, countdown);

    while (1) {
        /* Poll for keystroke, stall 100ms */
        g_bs->stall(100000); /* 100 ms */
        int key = read_key();

        if (key == KEY_UP) {
            user_acted = 1;
            selected = (selected - 1 + cfg.entry_count) % cfg.entry_count;
        } else if (key == KEY_DOWN) {
            user_acted = 1;
            selected = (selected + 1) % cfg.entry_count;
        } else if (key == KEY_ENTER || key == KEY_E) {
            user_acted = 1;
            break;
        } else if (key == KEY_ESC) {
            con_puts(u"\r\nasdboot: returning to firmware.\r\n");
            return EFI_SUCCESS;
        }

        if (!user_acted) {
            countdown--;
            if (countdown <= 0) break;
        }

        draw_menu(&cfg, selected, user_acted ? 0 : countdown);
    }

    st = build_and_handoff(&cfg.entries[selected]);
    if (st != EFI_SUCCESS) {
        con_puts_a("asdboot: failed to boot \"");
        con_puts_a(cfg.entries[selected].label);
        con_puts(u"\"\r\n");
        fallback_prompt(&cfg);
    }
    return EFI_LOAD_ERROR;
}
