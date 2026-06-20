/*
 * nettest — ICMP + network regression test for OpenASD.
 * Prints "NETTEST PING OK rtt=Nms" or "NETTEST PING FAIL".
 */
#include <asd/syscall.h>
#include <asd/stdio.h>
#include <stdint.h>

static void puts_asd(const char *s) {
    while (*s) { char c = *s++; asd_write(1, &c, 1); }
}

static void put_u32(uint32_t n) {
    char buf[16];
    int i = 15;
    buf[i] = '\0';
    if (n == 0) { buf[--i] = '0'; }
    while (n) { buf[--i] = (char)('0' + n % 10); n /= 10; }
    puts_asd(&buf[i]);
}

/* Parse dotted-decimal "a.b.c.d" → host byte order uint32 */
static uint32_t parse_ip(const char *s) {
    uint32_t ip = 0;
    for (int octet = 0; octet < 4; octet++) {
        if (octet > 0) {
            if (*s != '.') return 0;
            s++;
        }
        uint32_t val = 0;
        while (*s >= '0' && *s <= '9') val = val * 10 + (uint32_t)(*s++ - '0');
        if (val > 255) return 0;
        ip = (ip << 8) | val;
    }
    return ip;
}

int main(int argc, const char **argv) {
    /* Default: ping QEMU gateway */
    uint32_t dst = parse_ip("10.0.2.2");
    if (argc >= 2 && argv[1]) dst = parse_ip(argv[1]);

    puts_asd("NETTEST pinging gateway...\n");

    uint32_t rtt = 0;
    int r = asd_ping(dst, &rtt);
    if (r == 0) {
        puts_asd("NETTEST PING OK rtt=");
        put_u32(rtt);
        puts_asd("ms\n");
        asd_exit(0);
    } else {
        puts_asd("NETTEST PING FAIL\n");
        asd_exit(1);
    }
}
