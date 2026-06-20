#include "common.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("cat: missing operand");
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        int fd = asd_open(argv[i], O_READ);
        if (fd < 0) {
            printf("cat: %s: not found\n", argv[i]);
            continue;
        }
        char buf[512];
        long got;
        while ((got = asd_read(fd, buf, sizeof(buf))) > 0)
            asd_write(STDOUT_FD, buf, (size_t)got);
        asd_close(fd);
    }
    return 0;
}
