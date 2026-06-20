#include "common.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("rm: missing operand");
        return 1;
    }
    if (asd_unlink(argv[1]) != 0) {
        printf("rm: %s: failed\n", argv[1]);
        return 1;
    }
    return 0;
}
