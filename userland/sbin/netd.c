/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * netd — network daemon for OpenASD.
 * Announces presence via UDP, then loops receiving/echoing packets.
 */

#include <asd/syscall.h>
#include <asd/stdio.h>
#include <asd/types.h>
#include <stddef.h>
#include <stdint.h>

extern size_t strlen(const char *);

#define SYS_NET_SEND 30
#define SYS_NET_RECV 31

/* UDP send: dst_ip, dst_port, src_port, buf, len */
static int net_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                    const void *buf, uint16_t len) {
    return (int)__syscall(SYS_NET_SEND,
                          (long)dst_ip, (long)dst_port, (long)src_port,
                          (long)(uintptr_t)buf, (long)len);
}

/* UDP recv: buf, buf_sz, src_ip_ptr, src_port_ptr */
static int net_recv(void *buf, uint16_t buf_sz,
                    uint32_t *src_ip, uint16_t *src_port) {
    return (int)__syscall(SYS_NET_RECV,
                          (long)(uintptr_t)buf, (long)buf_sz,
                          (long)(uintptr_t)src_ip, (long)(uintptr_t)src_port, 0);
}

static void log_msg(const char *msg) {
    int fd = asd_open("/var/log/syslog", O_WRONLY | O_APPEND);
    if (fd >= 0) {
        asd_write(fd, msg, strlen(msg));
        asd_close(fd);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    log_msg("netd: starting\n");

    /* Announce to gateway (10.0.2.2) on UDP port 9 (discard) */
    const char *hello = "OpenASD netd hello";
    uint32_t gw_ip = (10u << 24) | (0u << 16) | (2u << 8) | 2u;
    int r = net_send(gw_ip, 9, 6000, hello, (uint16_t)strlen(hello));
    if (r > 0) {
        log_msg("netd: network OK, sent hello\n");
    } else {
        log_msg("netd: no network (virtio-net not found or ARP failed)\n");
    }

    /* Echo loop: receive UDP packets on port 6000, echo back */
    static uint8_t buf[1472];
    for (;;) {
        uint32_t src_ip = 0;
        uint16_t src_port = 0;
        int n = net_recv(buf, sizeof(buf), &src_ip, &src_port);
        if (n > 0 && src_ip != 0) {
            net_send(src_ip, src_port, 6000, buf, (uint16_t)n);
        }
        asd_yield();
    }

    asd_exit(0);
    return 0;
}
