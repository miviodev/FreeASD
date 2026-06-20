/*
 * SPDX-License-Identifier: MIT
 * mifetch — minimal OpenASD system info (raw syscalls only, no libasd stdio).
 */

#include <asd/syscall.h>
#include <stdint.h>
#include <stddef.h>

#define STDOUT 1

static void out(const char *s) {
    if (!s) return;
    const char *p = s;
    while (*p) p++;
    if (p > s)
        (void)asd_write(STDOUT, s, (size_t)(p - s));
}

static void outln(const char *s) {
    out(s);
    out("\n");
}

static void outch(char c) {
    (void)asd_write(STDOUT, &c, 1);
}

static void out_u32(uint32_t v) {
    char buf[12];
    int i = 0;
    if (v == 0) {
        outch('0');
        return;
    }
    while (v > 0) {
        buf[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i > 0)
        outch(buf[--i]);
}

static void out_uptime(uint64_t ticks_10ms) {
    uint64_t secs  = ticks_10ms / 100;
    uint64_t mins  = secs / 60;  secs  %= 60;
    uint64_t hours = mins / 60;  mins  %= 60;
    uint64_t days  = hours / 24; hours %= 24;

    if (days > 0) {
        out_u32((uint32_t)days);
        out("d ");
    }
    char buf[16];
    buf[0] = (char)('0' + (hours / 10) % 10);
    buf[1] = (char)('0' + hours % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + (mins / 10) % 10);
    buf[4] = (char)('0' + mins % 10);
    buf[5] = ':';
    buf[6] = (char)('0' + (secs / 10) % 10);
    buf[7] = (char)('0' + secs % 10);
    buf[8] = '\0';
    out(buf);
}

static void out_field(const char *key, const char *val) {
    out("  ");
    out(key);
    out(": ");
    outln(val);
}

int main(int argc, const char **argv, const char **envp) {
    (void)argc;
    (void)argv;
    (void)envp;

    outln("OpenASD mifetch");
    outln("");
    outln("   ___                  _   ____  ____  ");
    outln("  / _ \\ _ __   ___ _ __| | / ___||  _ \\ ");
    outln(" | | | | '_ \\ / _ \\ '_ \\ || |    | | | |");
    outln(" | |_| | |_) |  __/ | | | || |___| |_| |");
    outln("  \\___/| .__/ \\___|_| |_|_| \\____|____/ ");
    outln("       |_|                               ");
    outln("");

    static asd_utsname_t uts;
    for (size_t i = 0; i < sizeof(uts); i++)
        ((char *)&uts)[i] = 0;

    if (asd_uname(&uts) != 0) {
        outln("mifetch: uname failed");
        asd_exit(1);
    }

    const char *host = uts.nodename[0] ? uts.nodename : "openasd";
    const char *user = (asd_getuid() == 0) ? "root" : "user";

    out("  ");
    out(user);
    out("@");
    outln(host);
    outln("  --------");
    outln("");

    out_field("OS", uts.sysname[0] ? uts.sysname : "OpenASD");
    out_field("Kernel", uts.release[0] ? uts.release : "1.0");
    out("  Uptime: ");
    out_uptime(asd_time() / 10000000ULL);
    outln("");
    out_field("Host", host);
    out_field("Machine", uts.machine[0] ? uts.machine : "x86_64");
    out_field("CPU", "x86_64");
    out_field("Memory", "unknown");
    outln("");

    return 0;
}
