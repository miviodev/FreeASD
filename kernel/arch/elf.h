/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 */

#ifndef ASD_ELF_H
#define ASD_ELF_H

#include <stdint.h>
#include <stddef.h>
#include "../mm/mm.h"

/*
 * Load an ELF64 executable from memory into a new address space and
 * return the entry point virtual address.  The caller is responsible
 * for creating / switching the vmap.
 *
 * Returns 0 on error, entry RIP on success.
 */
uint64_t elf_load(const void *image, size_t image_size, vmap_t *vm);

/*
 * High-level: load + spawn a new process from an ELF image in VFS.
 * Returns new PID, or 0 on failure.
 */
uint32_t elf_spawn(const char *vfs_path, const char **argv,
                   const char **envp);

#endif /* ASD_ELF_H */
