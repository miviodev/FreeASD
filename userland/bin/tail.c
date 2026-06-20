/* tail — print last N lines for OpenASD */
#include <asd/syscall.h>
#include <stdint.h>

#define MAX_KEEP 1024
#define LINE_MAX_LEN 1024

static char  g_linebuf[MAX_KEEP][LINE_MAX_LEN];
static int   g_linelen[MAX_KEEP];
static int   g_head = 0;   /* ring buffer head */
static int   g_count = 0;  /* lines stored */

static void wstr(const char *s) { int n=0; while(s[n])n++; if(n) asd_write(1,s,(size_t)n); }

static int atoi_simple(const char *s) {
    int n = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

static void tail_fd(int fd, int keep) {
    if (keep > MAX_KEEP) keep = MAX_KEEP;
    g_head = 0; g_count = 0;
    char buf[4096];
    char tmp[LINE_MAX_LEN];
    int tpos = 0;
    for (;;) {
        long r = asd_read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        for (long i = 0; i < r; i++) {
            char c = buf[i];
            if (c == '\n' || tpos >= LINE_MAX_LEN - 1) {
                int slot = (g_head + g_count) % keep;
                for (int j = 0; j < tpos; j++) g_linebuf[slot][j] = tmp[j];
                g_linelen[slot] = tpos;
                if (g_count < keep) g_count++;
                else g_head = (g_head + 1) % keep;
                tpos = 0;
            } else {
                tmp[tpos++] = c;
            }
        }
    }
    /* flush partial */
    if (tpos > 0) {
        int slot = (g_head + g_count) % keep;
        for (int j = 0; j < tpos; j++) g_linebuf[slot][j] = tmp[j];
        g_linelen[slot] = tpos;
        if (g_count < keep) g_count++;
        else g_head = (g_head + 1) % keep;
    }
    for (int i = 0; i < g_count; i++) {
        int slot = (g_head + i) % keep;
        asd_write(1, g_linebuf[slot], (size_t)g_linelen[slot]);
        asd_write(1, "\n", 1);
    }
}

int main(int argc, const char **argv) {
    int count = 10;
    int argi  = 1;
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1]) {
        if (argv[argi][1] == 'n' && argi + 1 < argc) {
            count = atoi_simple(argv[argi + 1]);
            argi += 2;
        } else {
            count = atoi_simple(argv[argi] + 1);
            argi++;
        }
    }
    if (count <= 0) count = 10;
    if (argi >= argc) {
        tail_fd(0, count);
    } else {
        for (; argi < argc; argi++) {
            int fd = asd_open(argv[argi], O_RDONLY);
            if (fd < 0) { wstr("tail: cannot open: "); wstr(argv[argi]); wstr("\n"); continue; }
            tail_fd(fd, count); asd_close(fd);
        }
    }
    asd_exit(0);
    return 0;
}
