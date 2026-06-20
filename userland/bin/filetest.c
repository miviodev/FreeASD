/*
 * filetest — VFS write/read regression test for OpenASD.
 * Prints "FILETEST OK" on success, "FILETEST FAIL: <reason>" on error.
 */
#include <asd/syscall.h>
#include <asd/stdio.h>
#include <stdint.h>

static void puts_asd(const char *s) {
    while (*s) { char c = *s++; asd_write(1, &c, 1); }
}

static void put_int(int n) {
    char buf[16];
    int i = 15;
    buf[i] = '\0';
    if (n == 0) { buf[--i] = '0'; }
    while (n > 0) { buf[--i] = (char)('0' + n % 10); n /= 10; }
    puts_asd(&buf[i]);
}

static void fail(const char *reason, int code) {
    puts_asd("FILETEST FAIL: ");
    puts_asd(reason);
    puts_asd(" (");
    put_int(code < 0 ? -code : code);
    puts_asd(")\n");
    asd_exit(1);
}

int main(int argc, const char **argv) {
    (void)argc; (void)argv;
    const char *path = "/tmp/filetest.txt";
    const char *content = "hello filetest\n";
    int content_len = 15;

    /* --- Step 1: write --- */
    int fd = asd_open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) fail("open for write failed", fd);

    long wrote = asd_write(fd, content, (size_t)content_len);
    if (wrote != content_len) fail("write returned wrong count", (int)wrote);

    asd_close(fd);

    /* --- Step 2: read back --- */
    fd = asd_open(path, O_RDONLY);
    if (fd < 0) fail("open for read failed", fd);

    char buf[64];
    long got = asd_read(fd, buf, sizeof(buf) - 1);
    if (got < 0) fail("read failed", (int)got);
    buf[got] = '\0';
    asd_close(fd);

    /* --- Step 3: verify --- */
    if (got != content_len) fail("read size mismatch", (int)got);
    for (int i = 0; i < content_len; i++) {
        if (buf[i] != content[i]) fail("content mismatch", i);
    }

    /* --- Step 4: mkdir + file in subdir --- */
    asd_mkdir("/tmp/subdir");
    fd = asd_open("/tmp/subdir/x.txt", O_WRONLY | O_CREAT);
    if (fd < 0) fail("subdir create failed", fd);
    asd_write(fd, "x", 1);
    asd_close(fd);

    puts_asd("FILETEST OK\n");
    asd_exit(0);
}
