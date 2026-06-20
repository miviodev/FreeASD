/* grep — simple substring/pattern search for OpenASD */
#include <asd/syscall.h>
#include <stdint.h>

static char g_buf[65536];
static int  g_linebuf_n;
static char g_linebuf[1024];

static int  flag_n    = 0;  /* -n: show line numbers */
static int  flag_i    = 0;  /* -i: case insensitive  */
static int  flag_v    = 0;  /* -v: invert match      */
static int  flag_c    = 0;  /* -c: count only        */
static const char *pattern = 0;

static void wstr(int fd, const char *s, int n) { if (n > 0) asd_write(fd, s, (size_t)n); }

static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static char lc(char c) {
    if (c >= 'A' && c <= 'Z') return (char)(c + 32);
    return c;
}

static int match(const char *line, int llen, const char *pat, int plen) {
    if (plen == 0) return 1;
    if (llen < plen) return 0;
    for (int i = 0; i <= llen - plen; i++) {
        int ok = 1;
        for (int j = 0; j < plen; j++) {
            char a = flag_i ? lc(line[i+j]) : line[i+j];
            char b = flag_i ? lc(pat[j])    : pat[j];
            if (a != b) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static void wnum(int fd, long n) {
    char buf[24]; int i = 24;
    if (n == 0) { wstr(fd, "0", 1); return; }
    while (n > 0) { buf[--i] = '0' + (int)(n % 10); n /= 10; }
    wstr(fd, buf + i, 24 - i);
}

static long grep_fd(int fd, const char *fname, long *match_count) {
    int plen = slen(pattern);
    long linenum = 0;
    int pos = 0, avail = 0;
    char tmp[2]; tmp[1] = 0;

    for (;;) {
        if (pos >= avail) {
            long r = asd_read(fd, g_buf, sizeof(g_buf));
            if (r <= 0) break;
            avail = (int)r; pos = 0;
        }
        char c = g_buf[pos++];
        if (c == '\n' || g_linebuf_n >= (int)(sizeof(g_linebuf)-1)) {
            linenum++;
            int m = match(g_linebuf, g_linebuf_n, pattern, plen);
            if (flag_v) m = !m;
            if (m) {
                (*match_count)++;
                if (!flag_c) {
                    if (fname) { wstr(1, fname, slen(fname)); wstr(1, ":", 1); }
                    if (flag_n) { wnum(1, linenum); wstr(1, ":", 1); }
                    wstr(1, g_linebuf, g_linebuf_n); wstr(1, "\n", 1);
                }
            }
            g_linebuf_n = 0;
        } else {
            if (g_linebuf_n < (int)(sizeof(g_linebuf)-1))
                g_linebuf[g_linebuf_n++] = c;
        }
    }
    /* flush partial last line */
    if (g_linebuf_n > 0) {
        linenum++;
        int m = match(g_linebuf, g_linebuf_n, pattern, plen);
        if (flag_v) m = !m;
        if (m) {
            (*match_count)++;
            if (!flag_c) {
                if (fname) { wstr(1, fname, slen(fname)); wstr(1, ":", 1); }
                if (flag_n) { wnum(1, linenum); wstr(1, ":", 1); }
                wstr(1, g_linebuf, g_linebuf_n); wstr(1, "\n", 1);
            }
        }
        g_linebuf_n = 0;
    }
    return 0;
}

int main(int argc, const char **argv) {
    int argi = 1;
    /* parse flags */
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1]) {
        const char *f = argv[argi] + 1;
        while (*f) {
            if (*f == 'n') flag_n = 1;
            else if (*f == 'i') flag_i = 1;
            else if (*f == 'v') flag_v = 1;
            else if (*f == 'c') flag_c = 1;
            else { wstr(2, "grep: unknown flag -", 20); wstr(2, f, 1); wstr(2, "\n", 1); }
            f++;
        }
        argi++;
    }
    if (argi >= argc) {
        wstr(2, "usage: grep [-nicv] pattern [file...]\n", 37);
        asd_exit(2);
    }
    pattern = argv[argi++];

    long total_matches = 0;
    if (argi >= argc) {
        /* read stdin */
        grep_fd(0, 0, &total_matches);
    } else {
        int multi = (argc - argi) > 1;
        for (; argi < argc; argi++) {
            int fd = asd_open(argv[argi], O_RDONLY);
            if (fd < 0) { wstr(2, "grep: ", 6); wstr(2, argv[argi], slen(argv[argi])); wstr(2, ": no such file\n", 15); continue; }
            grep_fd(fd, multi ? argv[argi] : 0, &total_matches);
            asd_close(fd);
        }
    }
    if (flag_c) { wnum(1, total_matches); wstr(1, "\n", 1); }
    asd_exit(total_matches > 0 ? 0 : 1);
    return 0;
}
