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

#ifndef ASDBOOT_H
#define ASDBOOT_H

#include <stdint.h>

/* Magic values */
#define ASD_BIB_MAGIC    0xA5DB0071UL
#define ASD_BIB_VERSION  1
#define ASD_BOOT_MAGIC   0xA5DB007ULL  /* passed in rsi at kernel entry */

/* Framebuffer pixel formats */
#define ASD_FB_NONE    0
#define ASD_FB_RGBX32  1
#define ASD_FB_BGRX32  2

/* Memory region types */
#define ASD_MEM_FREE      0
#define ASD_MEM_RESERVED  1
#define ASD_MEM_ACPI_REC  2
#define ASD_MEM_ACPI_NVS  3
#define ASD_MEM_LOADER    4
#define ASD_MEM_KERNEL    5
#define ASD_MEM_INITRD    6
#define ASD_MEM_FB        7

/* Memory map entry */
typedef struct {
    uint64_t base;    /* Physical base address (page-aligned) */
    uint64_t pages;   /* Number of 4 KiB pages                */
    uint32_t type;    /* ASD_MEM_* constant                   */
    uint32_t _pad;
} asd_mmap_entry_t;

/* Boot Information Block — passed in rdi at kernel entry */
typedef struct {
    uint32_t magic;           /* ASD_BIB_MAGIC                         */
    uint16_t version;         /* ASD_BIB_VERSION                       */
    uint16_t size;            /* sizeof(asd_bib_t)                     */

    uint64_t mmap_phys;       /* Physical address of asd_mmap_entry_t[] */
    uint32_t mmap_count;
    uint32_t mmap_entry_sz;   /* sizeof(asd_mmap_entry_t)               */

    uint64_t fb_phys;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_stride;
    uint8_t  fb_format;       /* ASD_FB_* */
    uint8_t  fb_reserved[3];

    uint64_t acpi_rsdp_phys;

    uint64_t cmdline_phys;
    uint32_t cmdline_len;
    uint32_t _pad0;

    uint64_t initrd_phys;
    uint64_t initrd_len;

    uint64_t boot_ns;
} asd_bib_t;

#endif /* ASDBOOT_H */
