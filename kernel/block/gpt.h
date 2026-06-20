#ifndef ASD_GPT_H
#define ASD_GPT_H

#include "block.h"

/* Create a simple GPT layout on the given disk:
 * - protective MBR
 * - GPT with ESP (512MiB) + root (rest)
 * Returns 0 on success, non-zero on error.
 */
int gpt_init_single_disk(block_dev_t *dev);

/* Sector 0 is a FAT BPB (make install / live VFAT), not a blank disk. */
int disk_sector0_is_fat(const uint8_t *sec512);

#endif

