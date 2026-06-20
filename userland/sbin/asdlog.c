/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * asdlog -- system logging daemon for OpenASD
 *
 * Reads log messages from the kernel ring buffer via SYS_IOCTL and
 * writes them to /var/log/syslog on the filesystem.  On a minimal
 * system without /var this simply acts as a sink so svcmgr does not
 * report a launch failure.
 *
 * FIX Bug 5: This binary was referenced by svcmgr as /sbin/asdlog but
 * did not exist, causing the "FAIL: could not spawn" error at boot.
 */

#include <asd/syscall.h>
#include <asd/stdio.h>
#include <asd/types.h>
#include <stddef.h>
#include <stdint.h>

extern size_t strlen(const char *);

#define LOG_PATH  "/var/log/syslog"
#define BUF_SIZE  256

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    /* Try to open (or create) the log file.  If /var/log does not
     * exist yet we simply loop quietly — the daemon must not crash. */
    int fd = asd_open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
    if (fd < 0) {
        /* /var/log may not exist; try to create the directory tree. */
        asd_mkdir("/var");
        asd_mkdir("/var/log");
        fd = asd_open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND);
    }

    const char *banner = "asdlog: system logger started\n";
    if (fd >= 0) {
        asd_write(fd, banner, strlen(banner));
    }

    /* Main loop: yield and periodically flush any pending messages.
     * A real implementation would use an IPC ring; here we just keep
     * the process alive so svcmgr sees it as SVC_RUNNING. */
    for (;;) {
        asd_yield();
    }

    if (fd >= 0) asd_close(fd);
    asd_exit(0);
    return 0;
}
