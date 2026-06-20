#include "common.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        puts("mkdir: missing operand");
        return 1;
    }
    if (asd_mkdir(argv[1]) != 0) {
        printf("mkdir: %s: failed\n", argv[1]);
        return 1;
    }
    return 0;
}
