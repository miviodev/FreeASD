/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 */

#ifndef ASDINIT_H
#define ASDINIT_H

#include <stdint.h>
#include <stddef.h>

typedef uint32_t pid_t;

/* ------------------------------------------------------------------------- */

#define MAX_SERVICES     64
#define SVC_NAME_LEN     64
#define SVC_PATH_LEN     256
#define SVC_ARGS_LEN   1024
#define MAX_DEPS         16
#define MAX_ENV_VARS     32
#define ENV_NAME_LEN     64
#define ENV_VALUE_LEN    256

/* Service states */
#define SVC_NEW         0
#define SVC_STARTING    1
#define SVC_RUNNING     2
#define SVC_EXITED      3
#define SVC_RESTARTING  4
#define SVC_FAILED      5

/* Restart policies */
#define RESTART_NEVER   0
#define RESTART_ALWAYS  1
#define RESTART_ON_FAIL 2

/* Log types */
#define LOG_RING  0
#define LOG_FILE  1
#define LOG_NONE  2

/* ------------------------------------------------------------------------- */

typedef struct {
    char     name[SVC_NAME_LEN];
    char     binary[SVC_PATH_LEN];
    char     args[SVC_ARGS_LEN];    /* raw argument string */

    /* Dependencies */
    char     deps[MAX_DEPS][SVC_NAME_LEN];
    uint32_t dep_count;

    /* Configuration */
    uint8_t  restart;    /* RESTART_* */
    uint8_t  log_type;   /* LOG_* */
    uint8_t  state;      /* SVC_* */
    uint8_t  _pad;

    pid_t    pid;
    int32_t  exit_code;

    /* Environment variables */
    char     env_keys[MAX_ENV_VARS][ENV_NAME_LEN];
    char     env_vals[MAX_ENV_VARS][ENV_VALUE_LEN];
    uint32_t env_count;

    /* Identity — service runs as this user/group (empty = root) */
    char     svc_user[32];   /* username; empty means root (uid 0) */
    char     svc_group[32];  /* group name; empty means user's primary group */
} service_t;

/* ------------------------------------------------------------------------- */

void asdinit_main(void) __attribute__((noreturn));

#endif /* ASDINIT_H */
