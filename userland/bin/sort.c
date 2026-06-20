/* sort — sort lines of text for OpenASD */
#include <asd/syscall.h>
#include <stdint.h>

#define MAX_LINES 4096
#define MAX_LINE  1024
#define BUF_SZ    (MAX_LINES * MAX_LINE)

static char  g_storage[BUF_SZ];
static char *g_lines[MAX_LINES];
static int   g_nlines = 0;
static int   g_storage_used = 0;

static int  flag_r = 0;  /* -r: reverse */
static int  flag_u = 0;  /* -u: unique   */

static void wstr(const char *s) {
    int n = 0; while (s[n]) n++; if (n) asd_write(1, s, (size_t)n);
}

static int scmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static void qsort_lines(int lo, int hi) {
    if (lo >= hi) return;
    const char *pivot = g_lines[(lo + hi) / 2];
    int i = lo, j = hi;
    while (i <= j) {
        while (scmp(g_lines[i], pivot) < 0) i++;
        while (scmp(g_lines[j], pivot) > 0) j--;
        if (i <= j) {
            char *tmp = g_lines[i]; g_lines[i] = g_lines[j]; g_lines[j] = tmp;
            i++; j--;
        }
    }
    qsort_lines(lo, j);
    qsort_lines(i, hi);
}

static void read_lines(int fd) {
    char buf[4096];
    char linebuf[MAX_LINE];
    int lpos = 0;
    for (;;) {
        long r = asd_read(fd, buf, sizeof(buf));
        if (r <= 0) break;
        for (long i = 0; i < r; i++) {
            char c = buf[i];
            if (c == '\n' || lpos >= MAX_LINE - 1) {
                if (g_nlines >= MAX_LINES) continue;
                if (g_storage_used + lpos + 1 > BUF_SZ) continue;
                char *dst = g_storage + g_storage_used;
                for (int j = 0; j < lpos; j++) dst[j] = linebuf[j];
                dst[lpos] = 0;
                g_lines[g_nlines++] = dst;
                g_storage_used += lpos + 1;
                lpos = 0;
            } else {
                linebuf[lpos++] = c;
            }
        }
    }
    if (lpos > 0 && g_nlines < MAX_LINES && g_storage_used + lpos + 1 <= BUF_SZ) {
        char *dst = g_storage + g_storage_used;
        for (int j = 0; j < lpos; j++) dst[j] = linebuf[j];
        dst[lpos] = 0;
        g_lines[g_nlines++] = dst;
        g_storage_used += lpos + 1;
    }
}

int main(int argc, const char **argv) {
    int argi = 1;
    while (argi < argc && argv[argi][0] == '-') {
        const char *f = argv[argi] + 1;
        while (*f) {
            if (*f == 'r') flag_r = 1;
            else if (*f == 'u') flag_u = 1;
            f++;
        }
        argi++;
    }

    if (argi >= argc) {
        read_lines(0);
    } else {
        for (; argi < argc; argi++) {
            int fd = asd_open(argv[argi], O_RDONLY);
            if (fd < 0) { wstr("sort: cannot open: "); wstr(argv[argi]); wstr("\n"); continue; }
            read_lines(fd); asd_close(fd);
        }
    }

    if (g_nlines > 1) qsort_lines(0, g_nlines - 1);

    const char *prev = 0;
    for (int i = flag_r ? g_nlines - 1 : 0;
         flag_r ? i >= 0 : i < g_nlines;
         flag_r ? i-- : i++) {
        const char *line = g_lines[i];
        if (flag_u && prev && scmp(line, prev) == 0) continue;
        wstr(line); asd_write(1, "\n", 1);
        prev = line;
    }
    asd_exit(0);
    return 0;
}
