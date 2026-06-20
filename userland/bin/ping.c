/*
 * ping — ICMP echo utility for OpenASD.
 * Usage: ping <ip>  or  ping <a.b.c.d>
 */

#include <asd/syscall.h>
#include <asd/stdio.h>
#include <stdint.h>

static void put_str(const char *s) {
    while (*s) {
        char c = *s++;
        asd_write(1, &c, 1);
    }
}

static void put_u32(uint32_t n) {
    char buf[12];
    int i = 11;
    buf[i] = '\0';
    if (n == 0) { buf[--i] = '0'; }
    while (n) { buf[--i] = (char)('0' + n % 10); n /= 10; }
    put_str(&buf[i]);
}

/* Parse dotted-decimal "a.b.c.d" → host byte order uint32.
 * Returns 0 on parse error (0.0.0.0 is also an error here). */
static uint32_t parse_ip(const char *s) {
    uint32_t ip = 0;
    for (int octet = 0; octet < 4; octet++) {
        if (octet > 0) {
            if (*s != '.') return 0;
            s++;
        }
        uint32_t val = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            val = val * 10 + (uint32_t)(*s++ - '0');
            digits++;
        }
        if (digits == 0 || val > 255) return 0;
        ip = (ip << 8) | val;
    }
    return (*s == '\0') ? ip : 0;
}

static void print_ip(uint32_t ip) {
    put_u32((ip >> 24) & 0xFF); put_str(".");
    put_u32((ip >> 16) & 0xFF); put_str(".");
    put_u32((ip >>  8) & 0xFF); put_str(".");
    put_u32( ip        & 0xFF);
}

int main(int argc, const char **argv) {
    if (argc < 2) {
        put_str("usage: ping <ip>\n");
        asd_exit(1);
    }

    uint32_t dst = parse_ip(argv[1]);
    if (dst == 0) {
        put_str("ping: invalid address: ");
        put_str(argv[1]);
        put_str("\n");
        asd_exit(1);
    }

    put_str("PING ");
    print_ip(dst);
    put_str("\n");

    int count = 4;
    int ok = 0;

    for (int i = 1; i <= count; i++) {
        uint32_t rtt = 0;
        int r = asd_ping(dst, &rtt);
        put_str(r == 0 ? "64 bytes from " : "Request timeout for icmp_seq ");
        if (r == 0) {
            print_ip(dst);
            put_str(": icmp_seq=");
            put_u32((uint32_t)i);
            put_str(" ttl=64 time=");
            put_u32(rtt);
            put_str(" ms\n");
            ok++;
        } else {
            put_u32((uint32_t)i);
            put_str("\n");
        }
    }

    put_str("\n--- ");
    print_ip(dst);
    put_str(" ping statistics ---\n");
    put_u32((uint32_t)count);
    put_str(" packets transmitted, ");
    put_u32((uint32_t)ok);
    put_str(" received, ");
    put_u32((uint32_t)((count - ok) * 100 / count));
    put_str("% packet loss\n");

    asd_exit(ok > 0 ? 0 : 1);
}
