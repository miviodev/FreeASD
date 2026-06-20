#ifndef ASD_BLOCK_H
#define ASD_BLOCK_H

#include <stdint.h>
#include <stddef.h>

#define BLOCK_MAX_DEVS 8
#define BLOCK_NAME_LEN 32

typedef struct block_dev block_dev_t;

typedef int (*block_read_fn)(block_dev_t *dev, uint64_t lba, uint32_t count, void *buf);
typedef int (*block_write_fn)(block_dev_t *dev, uint64_t lba, uint32_t count, const void *buf);

struct block_dev {
    char           name[BLOCK_NAME_LEN];
    uint32_t       sector_size;
    uint64_t       sector_count;
    void          *ctx;
    block_read_fn  read;
    block_write_fn write;
};

void block_init(void);
int  block_register(const block_dev_t *tmpl);
block_dev_t *block_find(const char *name);
block_dev_t *block_first(void);
int block_count(void);
block_dev_t *block_get(int idx);

#endif
