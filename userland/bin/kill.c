/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * kill — send a signal to a process.
 *
 * Usage: kill [-9] <pid>
 *
 * NOTE: OpenASD does not yet have a SYS_KILL syscall.  This utility
 * serves as a placeholder that demonstrates the intended interface and
 * will become functional once SYS_KILL is implemented in the kernel.
 */
#include "common.h"

static int parse_int(const char *s) {
    int n = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

int main(int argc, const char **argv) {
    if (argc < 2) {
        puts("usage: kill [-9] <pid>");
        return 1;
    }

    int sig = 15;  /* SIGTERM */
    int pid_idx = 1;

    if (argv[1][0] == '-') {
        int s = parse_int(argv[1] + 1);
        if (s == 9) sig = 9;
        pid_idx = 2;
    }

    if (pid_idx >= argc) {
        puts("kill: missing pid");
        return 1;
    }

    int pid = parse_int(argv[pid_idx]);
    if (pid <= 0) {
        printf("kill: invalid pid: %s\n", argv[pid_idx]);
        return 1;
    }

    /* SYS_KILL not yet implemented — print informational message */
    printf("kill: would send signal %d to pid %d (SYS_KILL not yet implemented)\n",
           sig, pid);
    return 0;
}
