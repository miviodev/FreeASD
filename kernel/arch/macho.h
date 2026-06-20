/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 */

#ifndef ASD_MACHO_H
#define ASD_MACHO_H

#include <stdint.h>
#include <stddef.h>
#include "../mm/mm.h"

/*
 * Load a Mach-O 64-bit executable from memory into a new address space and
 * return the entry point virtual address.
 *
 * Returns 0 on error, entry RIP on success.
 */
uint64_t macho_load(const void *image, size_t image_size, vmap_t *vm);

/*
 * High-level: load + spawn a new process from a Mach-O image in VFS.
 * Returns new PID, or 0 on failure.
 */
uint32_t macho_spawn(const char *vfs_path, const char **argv,
                     const char **envp);

#endif /* ASD_MACHO_H */
