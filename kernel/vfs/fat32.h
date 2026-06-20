#ifndef ASD_FAT32_H
#define ASD_FAT32_H

#include <stdint.h>
#include <stddef.h>

/* Mount a memory-backed FAT32 image as read-only backend. */
uint32_t fat32_mount_image(const char *mount_pt, const void *image, size_t image_size);

/* Create or replace an 8.3 root-level file inside a memory FAT image (installer). */
int fat32_image_upsert_file(void *image, size_t image_size,
                            const char *name83, const void *data, size_t data_len);

#endif /* ASD_FAT32_H */
