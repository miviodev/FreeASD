/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 */

#include "gdt.h"
#include <stdint.h>

/* IA-32e TSS (104 bytes, padded to 112 for alignment) */
typedef struct {
    uint32_t _res0;
    uint64_t rsp0;        /* kernel stack for ring-0 entry */
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t _res1;
    uint64_t ist[7];      /* IST stacks (unused for now) */
    uint64_t _res2;
    uint16_t _res3;
    uint16_t iopb;        /* I/O permission bitmap offset (set past end → no IOPB) */
} __attribute__((packed)) tss64_t;

/* GDT: 6 normal entries (8 bytes each) + 1 TSS (16 bytes) = 64 bytes */
static uint64_t g_gdt[8] __attribute__((aligned(16)));
static tss64_t  g_tss    __attribute__((aligned(16)));

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdtr_t;

static uint64_t make_seg(uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t flags)
{
    return ((uint64_t)(limit  & 0x0000FFFF)      )
         | ((uint64_t)(base   & 0x0000FFFF) << 16)
         | ((uint64_t)((base  >> 16) & 0xFF) << 32)
         | ((uint64_t)access                << 40)
         | ((uint64_t)((limit >> 16) & 0x0F) << 48)
         | ((uint64_t)(flags  & 0x0F)        << 52)
         | ((uint64_t)((base  >> 24) & 0xFF) << 56);
}

void gdt_init(void) {
    /* 0: null */
    g_gdt[0] = 0;

    /* 1: kernel code — DPL=0, 64-bit, readable
     *    access=0x9A (P|S|type=1010), flags=0xA (G|L) */
    g_gdt[1] = make_seg(0, 0xFFFFF, 0x9A, 0xA);

    /* 2: kernel data — DPL=0, writable
     *    access=0x92 (P|S|type=0010), flags=0xC (G|DB) */
    g_gdt[2] = make_seg(0, 0xFFFFF, 0x92, 0xC);

    /* 3: user code 32-bit (compatibility mode) — DPL=3
     *    access=0xFA (P|DPL3|S|type=1010), flags=0xC (G|DB=1, L=0) */
    g_gdt[3] = make_seg(0, 0xFFFFF, 0xFA, 0xC);

    /* 4: user data — DPL=3
     *    access=0xF2 (P|DPL3|S|type=0010), flags=0xC */
    g_gdt[4] = make_seg(0, 0xFFFFF, 0xF2, 0xC);

    /* 5: user code 64-bit — DPL=3
     *    access=0xFA (P|DPL3|S|type=1010), flags=0xA */
    g_gdt[5] = make_seg(0, 0xFFFFF, 0xFA, 0xA);

    /* 6-7: TSS descriptor (16 bytes = two slots) */
    uintptr_t tss_base  = (uintptr_t)&g_tss;
    uint32_t  tss_limit = (uint32_t)(sizeof(g_tss) - 1);

    /* TSS access: 0x89 = P|type=1001 (64-bit available TSS) */
    g_gdt[6] = make_seg((uint32_t)tss_base, tss_limit, 0x89, 0x0);
    /* Upper 32 bits of base in slot 7 */
    g_gdt[7] = (uint64_t)((tss_base >> 32) & 0xFFFFFFFF);

    /* IOPB past end → deny all I/O from ring-3 (for now) */
    g_tss.iopb = (uint16_t)sizeof(g_tss);

    gdtr_t gdtr = {
        .limit = (uint16_t)(sizeof(g_gdt) - 1),
        .base  = (uint64_t)(uintptr_t)g_gdt,
    };

    __asm__ volatile(
        "lgdt %0\n\t"
        /* Reload CS via a far return */
        "pushq %1\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        /* Reload data segments */
        "movw %2, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "movw $0, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        /* Load TSS */
        "movw %3, %%ax\n\t"
        "ltr %%ax\n\t"
        :
        : "m"(gdtr),
          "i"((uint64_t)SEG_KERNEL_CODE),
          "i"((uint16_t)SEG_KERNEL_DATA),
          "i"((uint16_t)SEG_TSS)
        : "rax", "memory"
    );
}

void tss_set_rsp0(uint64_t rsp0) {
    g_tss.rsp0 = rsp0;
}
