#include "block.h"
#include <string.h>

static block_dev_t g_block_devs[BLOCK_MAX_DEVS];

void block_init(void) {
    memset(g_block_devs, 0, sizeof(g_block_devs));
}

int block_register(const block_dev_t *tmpl) {
    if (!tmpl || !tmpl->name[0] || !tmpl->read || !tmpl->write || tmpl->sector_size == 0)
        return -1;
    for (int i = 0; i < BLOCK_MAX_DEVS; i++) {
        if (g_block_devs[i].name[0] == '\0') {
            g_block_devs[i] = *tmpl;
            return 0;
        }
    }
    return -1;
}

block_dev_t *block_find(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < BLOCK_MAX_DEVS; i++) {
        if (g_block_devs[i].name[0] && strcmp(g_block_devs[i].name, name) == 0)
            return &g_block_devs[i];
    }
    return NULL;
}

block_dev_t *block_first(void) {
    for (int i = 0; i < BLOCK_MAX_DEVS; i++) {
        if (g_block_devs[i].name[0]) return &g_block_devs[i];
    }
    return NULL;
}

int block_count(void) {
    int n = 0;
    for (int i = 0; i < BLOCK_MAX_DEVS; i++) {
        if (g_block_devs[i].name[0]) n++;
    }
    return n;
}

block_dev_t *block_get(int idx) {
    int n = 0;
    for (int i = 0; i < BLOCK_MAX_DEVS; i++) {
        if (!g_block_devs[i].name[0]) continue;
        if (n == idx) return &g_block_devs[i];
        n++;
    }
    return NULL;
}
