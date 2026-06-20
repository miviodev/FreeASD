/*
 * hxtest — full hx save regression test.
 *
 * Tests the exact save flow from hx editor.rs:
 *   1. open(path, O_WRONLY|O_CREAT|O_TRUNC)
 *   2. for each line: if !empty { write(line) }; write("\n")
 *   3. close
 * Then verifies content and ls visibility.
 * Also tests: multi-line, overwrite, empty-line handling.
 *
 * Output: "HXTEST OK" or "HXTEST FAIL: <reason> code=<n>"
 */
#include <asd/syscall.h>
#include <stdint.h>

static void wch(char c) { asd_write(1, &c, 1); }
static void wstr(const char *s, int n) { asd_write(1, s, (size_t)n); }

static void wnum(long n) {
    char buf[24]; int i = 24; int neg = (n < 0);
    if (neg) n = -n;
    if (n == 0) { wch('0'); return; }
    while (n > 0) { buf[--i] = '0' + (int)(n % 10); n /= 10; }
    if (neg) buf[--i] = '-';
    wstr(buf + i, 24 - i);
}

static void fail(const char *msg, int mlen, long code) {
    wstr("HXTEST FAIL: ", 13); wstr(msg, mlen);
    wstr(" code=", 6); wnum(code); wch('\n');
    asd_exit(1);
}

/* Compare two byte arrays */
static int xmemcmp(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 1;
    return 0;
}

/* Write a single line + newline exactly like hx save() does */
static void hx_writeline(int fd, const char *line, int len) {
    if (len > 0) {
        long w = asd_write(fd, line, (size_t)len);
        if (w != len) { asd_close(fd); fail("write line", 10, w); }
    }
    long w = asd_write(fd, "\n", 1);
    if (w != 1) { asd_close(fd); fail("write newline", 13, w); }
}

int main(int argc, const char **argv) {
    (void)argc; (void)argv;

    /* ---- Test A: basic 3-line file ---- */
    wstr("A: basic write\n", 15);
    {
        const char path[] = "/hxtest_a.txt";
        int fd = asd_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) fail("A open", 6, fd);
        hx_writeline(fd, "hello world", 11);
        hx_writeline(fd, "second line", 11);
        hx_writeline(fd, "third line",  10);
        asd_close(fd);

        /* verify */
        asd_stat_t st;
        if (asd_stat(path, &st) != 0) fail("A stat", 6, -1);
        /* "hello world\n"=12, "second line\n"=12, "third line\n"=11 → 35 */
        if (st.size != 35) fail("A size", 6, st.size);

        int rfd = asd_open(path, O_RDONLY);
        if (rfd < 0) fail("A read open", 11, rfd);
        char buf[64];
        long got = asd_read(rfd, buf, 63);
        asd_close(rfd);
        if (got != 35) fail("A read size", 11, got);
        if (xmemcmp(buf, "hello world\nsecond line\nthird line\n", 35))
            fail("A content", 9, 0);
        wstr("A OK\n", 5);
    }

    /* ---- Test B: overwrite existing file (simulates :w second time) ---- */
    wstr("B: overwrite\n", 13);
    {
        const char path[] = "/hxtest_b.txt";

        /* First write: 3 lines */
        int fd = asd_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) fail("B open1", 7, fd);
        hx_writeline(fd, "original content", 16);
        hx_writeline(fd, "more original", 13);
        asd_close(fd);

        /* Second write: fewer lines (simulates editing and saving) */
        fd = asd_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) fail("B open2", 7, fd);
        hx_writeline(fd, "new content", 11);
        asd_close(fd);

        /* verify: only "new content\n" should be there */
        asd_stat_t st;
        if (asd_stat(path, &st) != 0) fail("B stat", 6, -1);
        if (st.size != 12) fail("B size", 6, st.size);

        int rfd = asd_open(path, O_RDONLY);
        if (rfd < 0) fail("B read", 6, rfd);
        char buf[32];
        long got = asd_read(rfd, buf, 31);
        asd_close(rfd);
        if (got != 12) fail("B read size", 11, got);
        if (xmemcmp(buf, "new content\n", 12)) fail("B content", 9, 0);
        wstr("B OK\n", 5);
    }

    /* ---- Test C: empty lines (hx writes \n even for empty buf lines) ---- */
    wstr("C: empty lines\n", 15);
    {
        const char path[] = "/hxtest_c.txt";
        int fd = asd_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) fail("C open", 6, fd);
        /* hx buffer starts with count=1, first line empty */
        hx_writeline(fd, "",  0); /* empty line → just "\n" */
        hx_writeline(fd, "text", 4);
        hx_writeline(fd, "", 0);  /* empty line */
        asd_close(fd);

        asd_stat_t st;
        if (asd_stat(path, &st) != 0) fail("C stat", 6, -1);
        /* "\n" + "text\n" + "\n" = 1+5+1 = 7 */
        if (st.size != 7) fail("C size", 6, st.size);
        wstr("C OK\n", 5);
    }

    /* ---- Test D: ls / shows our files ---- */
    wstr("D: ls /\n", 8);
    {
        asd_dirent_t ents[16];
        uint32_t n = 0;
        int r = asd_readdir("/", ents, 16, &n);
        if (r != 0) fail("D readdir", 9, r);
        wstr("D: entries=", 11); wnum((long)n); wch('\n');

        int found_a = 0, found_b = 0, found_c = 0;
        for (uint32_t i = 0; i < n; i++) {
            const char *nm = ents[i].name;
            /* check for hxtest_a.txt */
            const char wa[] = "hxtest_a.txt";
            int ma = 1; for (int j = 0; j < 12; j++) if (nm[j]!=wa[j]) { ma=0; break; }
            if (ma && nm[12]==0) found_a = 1;
            const char wb[] = "hxtest_b.txt";
            int mb = 1; for (int j = 0; j < 12; j++) if (nm[j]!=wb[j]) { mb=0; break; }
            if (mb && nm[12]==0) found_b = 1;
            const char wc[] = "hxtest_c.txt";
            int mc = 1; for (int j = 0; j < 12; j++) if (nm[j]!=wc[j]) { mc=0; break; }
            if (mc && nm[12]==0) found_c = 1;
        }
        if (!found_a) fail("D no file a", 11, 0);
        if (!found_b) fail("D no file b", 11, 0);
        if (!found_c) fail("D no file c", 11, 0);
        wstr("D OK\n", 5);
    }

    /* ---- Test E: large file (simulate hx reading >4096 byte file) ---- */
    wstr("E: large file read\n", 19);
    {
        const char path[] = "/hxtest_e.txt";
        /* Write 100 lines of 80 chars each = 8100 bytes */
        int fd = asd_open(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) fail("E open write", 12, fd);
        for (int i = 0; i < 100; i++) {
            char line[80];
            for (int j = 0; j < 79; j++) line[j] = 'A' + (char)((i + j) % 26);
            line[79] = 0;
            long w = asd_write(fd, line, 79);
            if (w != 79) { asd_close(fd); fail("E write line", 12, w); }
            w = asd_write(fd, "\n", 1);
            if (w != 1) { asd_close(fd); fail("E write nl", 10, w); }
        }
        asd_close(fd);

        asd_stat_t st;
        if (asd_stat(path, &st) != 0) fail("E stat", 6, -1);
        if (st.size != 8000) fail("E size", 6, st.size); /* 100*(79+1)=8000 */

        /* Read back — this tests the sys_read loop fix */
        fd = asd_open(path, O_RDONLY);
        if (fd < 0) fail("E open read", 11, fd);
        char bigbuf[8100];
        long got = asd_read(fd, bigbuf, 8100);
        asd_close(fd);
        if (got != 8000) fail("E read size", 11, got);
        /* verify first line */
        if (bigbuf[79] != '\n') fail("E first nl", 10, bigbuf[79]);
        wstr("E OK (large file read/write works)\n", 35);
    }

    /* ---- Test F: relative paths resolve against CWD, not always "/" ---- */
    wstr("F: cwd-relative paths\n", 22);
    {
        int r = asd_mkdir("/hxtest_dir");
        if (r != 0) fail("F mkdir", 7, r);

        r = asd_chdir("/hxtest_dir");
        if (r != 0) fail("F chdir", 7, r);

        char cwdbuf[64];
        int n = asd_getcwd(cwdbuf, sizeof(cwdbuf));
        if (n < 0) fail("F getcwd", 8, n);
        cwdbuf[n] = 0;
        wstr("    cwd=", 8); wstr(cwdbuf, n); wch('\n');
        if (n != 11 || xmemcmp(cwdbuf, "/hxtest_dir", 11)) fail("F cwd mismatch", 14, n);

        /* open a RELATIVE path — must land inside /hxtest_dir, not "/" */
        int fd = asd_open("relfile.txt", O_WRONLY | O_CREAT | O_TRUNC);
        if (fd < 0) fail("F open rel", 10, fd);
        long w = asd_write(fd, "in subdir\n", 10);
        if (w != 10) { asd_close(fd); fail("F write rel", 11, w); }
        asd_close(fd);

        /* must be visible via absolute path /hxtest_dir/relfile.txt */
        asd_stat_t st;
        if (asd_stat("/hxtest_dir/relfile.txt", &st) != 0)
            fail("F not at abs path", 17, 0);
        if (st.size != 10) fail("F abs size", 10, st.size);

        /* must NOT have been created at root "/relfile.txt" */
        asd_stat_t st2;
        if (asd_stat("/relfile.txt", &st2) == 0)
            fail("F leaked to root", 16, 0);

        /* cd back to root and verify cwd resets */
        r = asd_chdir("/");
        if (r != 0) fail("F chdir back", 13, r);
        n = asd_getcwd(cwdbuf, sizeof(cwdbuf));
        if (n != 1 || cwdbuf[0] != '/') fail("F cwd not root", 14, n);

        wstr("F OK (cwd-relative paths work)\n", 32);
    }

    wstr("HXTEST OK\n", 10);
    asd_exit(0);
    return 0;
}
