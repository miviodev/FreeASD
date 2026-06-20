/* head — print first N lines for OpenASD */
#include <asd/syscall.h>
#include <stdint.h>

static void wstr(const char *s) { int n=0; while(s[n])n++; if(n) asd_write(1,s,(size_t)n); }

static int atoi_simple(const char *s) {
    int n = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

static void head_fd(int fd, int count) {
    char buf[4096];
    char linebuf[1024];
    int lpos = 0, lines = 0;
    for (;;) {
        long r = asd_read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        for (long i = 0; i < r; i++) {
            char c = buf[i];
            if (c == '\n' || lpos >= 1023) {
                linebuf[lpos] = 0;
                wstr(linebuf); asd_write(1, "\n", 1);
                lpos = 0;
                if (++lines >= count) return;
            } else {
                linebuf[lpos++] = c;
            }
        }
    }
    /* flush partial last line */
    if (lpos > 0 && lines < count) {
        linebuf[lpos] = 0; wstr(linebuf); asd_write(1, "\n", 1);
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
            /* -N shorthand */
            count = atoi_simple(argv[argi] + 1);
            argi++;
        }
    }
    if (count <= 0) count = 10;
    if (argi >= argc) {
        head_fd(0, count);
    } else {
        for (; argi < argc; argi++) {
            int fd = asd_open(argv[argi], O_RDONLY);
            if (fd < 0) { wstr("head: cannot open: "); wstr(argv[argi]); wstr("\n"); continue; }
            head_fd(fd, count); asd_close(fd);
        }
    }
    asd_exit(0);
    return 0;
}
