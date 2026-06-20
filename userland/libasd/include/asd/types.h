/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 */

#ifndef LIBASD_TYPES_H
#define LIBASD_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int64_t  ssize_t;
typedef uint32_t mode_t;
typedef uint32_t pid_t;

/* VFS dirent (must match kernel vfs.h) */
#define DIRENT_NAME_LEN 256
typedef struct {
    char    name[DIRENT_NAME_LEN];
    uint8_t kind;
    int64_t size;
} asd_dirent_t;

#define ASD_NODE_FILE 1
#define ASD_NODE_DIR  2

/* stat (must match kernel stat_info_t) */
#define PATH_MAX 256
typedef struct {
    int64_t  size;
    uint8_t  kind;
    uint64_t mtime_ns;
    char     name[PATH_MAX];
    uint16_t uid;
    uint16_t gid;
    uint16_t mode;
    uint8_t  _pad[2];
} asd_stat_t;

/* exit_info (must match kernel exit_info_t) */
typedef struct {
    int32_t  exit_code;
    uint64_t cpu_ns;
    uint64_t wall_ns;
} asd_exit_info_t;

#endif /* LIBASD_TYPES_H */
