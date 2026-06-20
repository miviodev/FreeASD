/*
 * ASD Fast File System (FFS/UFS-inspired)
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef ASD_FFS_H
#define ASD_FFS_H

#include "vfs.h"
#include "../block/block.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */

#define FFS_MAGIC           0x19960317U
#define FFS_BLOCK_SIZE      4096U
#define FFS_INODE_SIZE      128U
#define FFS_DIRECT_BLOCKS   12U
#define FFS_INODES_PER_BLK  (FFS_BLOCK_SIZE / FFS_INODE_SIZE)   /* 32 */
#define FFS_PTRS_PER_BLK    (FFS_BLOCK_SIZE / sizeof(uint32_t)) /* 1024 */

/* inode mode bits */
#define FFS_IFDIR   0040000U
#define FFS_IFREG   0100000U

/* ------------------------------------------------------------------ */
/* On-disk structures                                                   */
/* ------------------------------------------------------------------ */

/*
 * Superblock — stored at block 1 (byte offset FFS_BLOCK_SIZE).
 * All fields are little-endian.
 *
 * Partition layout (block numbers):
 *   0                  : reserved / boot block
 *   1                  : superblock  (this struct, padded to FFS_BLOCK_SIZE)
 *   inode_bitmap_start : inode allocation bitmap (inode_bitmap_blocks blocks)
 *   block_bitmap_start : data-block allocation bitmap (block_bitmap_blocks blocks)
 *   inode_table_start  : inode table
 *   data_start         : data blocks
 */
typedef struct {
    uint32_t magic;
    uint32_t block_size;          /* always FFS_BLOCK_SIZE */
    uint32_t total_blocks;        /* total blocks in partition */
    uint32_t inode_count;         /* total inodes (including reserved) */
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t inode_bitmap_start;  /* block# where inode bitmap begins */
    uint32_t inode_bitmap_blocks; /* number of inode bitmap blocks */
    uint32_t block_bitmap_start;  /* block# where block bitmap begins */
    uint32_t block_bitmap_blocks; /* number of block bitmap blocks */
    uint32_t inode_table_start;   /* block# of first inode table block */
    uint32_t data_start;          /* block# of first data block */
    uint32_t _reserved[20];       /* pad to 128 bytes */
} __attribute__((packed)) ffs_superblock_t;

typedef struct {
    uint16_t mode;                        /* FFS_IFREG or FFS_IFDIR */
    uint16_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;                        /* file size in bytes */
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t blocks[FFS_DIRECT_BLOCKS];   /* direct data block numbers */
    uint32_t indirect_block;              /* single-indirect pointer */
    uint32_t double_indirect_block;       /* double-indirect pointer (unused) */
} __attribute__((packed)) ffs_inode_t;

/*
 * Directory entry stored sequentially inside a data block.
 * rec_len is the total byte length of this entry (name + header, 4-byte aligned).
 * inode == 0 means the slot is free / deleted.
 */
typedef struct {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;   /* 1=file 2=dir */
    char     name[255];
} __attribute__((packed)) ffs_dirent_t;

#define FFS_DIRENT_HDR  8U   /* sizeof fields before name[] */

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

/*
 * Format a partition range on `dev` starting at `first_lba` (512-byte
 * sectors), spanning `sector_count` sectors, as a fresh FFS volume.
 * Returns 0 on success.
 */
int ffs_format(block_dev_t *dev, uint64_t first_lba, uint64_t sector_count);

/*
 * Mount an existing FFS volume.  Returns the VFS backend ID (>0) on
 * success, or 0 on error.
 */
uint32_t ffs_mount_dev(const char *mount_pt, block_dev_t *dev,
                       uint64_t first_lba, uint64_t sector_count);

/*
 * Legacy init (reads from block 1 of `dev` at LBA 0).
 * Kept for backward compatibility; prefer ffs_mount_dev.
 */
void ffs_init(block_dev_t *dev);

#endif /* ASD_FFS_H */
