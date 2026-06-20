#include "common.h"

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    static asd_dirent_t ents[256];
    uint32_t n = 0;
    int r = asd_readdir(path, ents, 256, &n);
    if (r != 0) {
        printf("ls: %s: no such directory\n", path);
        return 1;
    }
    for (uint32_t i = 0; i < n; i++) {
        if (ents[i].kind == ASD_NODE_DIR)
            printf("d  %s/\n", ents[i].name);
        else
            printf("-  %s  (%lld B)\n", ents[i].name, (long long)ents[i].size);
    }
    return 0;
}
