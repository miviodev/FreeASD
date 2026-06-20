#include "common.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("touch: missing operand");
        return 1;
    }
    int fd = asd_open(argv[1], O_WRITE | O_CREAT);
    if (fd < 0) {
        printf("touch: %s: failed\n", argv[1]);
        return 1;
    }
    asd_close(fd);
    return 0;
}
