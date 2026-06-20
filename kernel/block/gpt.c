/*
 * Minimal GPT writer for ASD installer.
 *
 * Layout:
 * - LBA0: protective MBR
 * - LBA1: primary GPT header
 * - LBA2..LBA33: partition entries (128 * 128 bytes)
 * - last 34 LBAs: backup entries + backup header
 *
 * Partitions:
 * - #1: ESP (512 MiB), type EFI System
 * - #2: ASD root (rest), type Linux filesystem data
 */

#include "gpt.h"
#include "../mm/mm.h"
#include "../include/stdint.h"
#include "../include/stddef.h"
#include "../include/string.h"

#define ESP_SIZE_BYTES   (512ULL * 1024ULL * 1024ULL)
#define ESP_SIZE_SECTORS (ESP_SIZE_BYTES / 512ULL)

/* CRC32 (polynomial 0xEDB88320) */
static uint32_t crc32_table[256];
static int crc32_init_done;

static void crc32_init(void) {
    if (crc32_init_done) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            if (c & 1) c = (c >> 1) ^ 0xEDB88320U;
            else       c >>= 1;
        }
        crc32_table[i] = c;
    }
    crc32_init_done = 1;
}

static uint32_t crc32_compute(const void *buf, size_t len) {
    crc32_init();
    const uint8_t *p = (const uint8_t *)buf;
    uint32_t c = 0xFFFFFFFFU;
    while (len--) {
        c = crc32_table[(c ^ *p++) & 0xFFU] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFU;
}

#pragma pack(push, 1)
typedef struct {
    uint8_t  boot_code[440];
    uint32_t disk_id;
    uint16_t reserved;
    struct {
        uint8_t  status;
        uint8_t  chs_first[3];
        uint8_t  type;
        uint8_t  chs_last[3];
        uint32_t lba_first;
        uint32_t lba_count;
    } part[4];
    uint16_t sig;
} mbr_t;

typedef struct {
    uint8_t  signature[8];
    uint32_t revision;
    uint32_t header_size;
    uint32_t header_crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t part_entry_lba;
    uint32_t num_parts;
    uint32_t part_entry_size;
    uint32_t part_array_crc32;
    uint8_t  _pad[420];
} gpt_header_t;

typedef struct {
    uint8_t  type_guid[16];
    uint8_t  part_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t attrs;
    uint16_t name[36];
} gpt_part_t;
#pragma pack(pop)

static void guid_from_bytes(uint8_t out[16],
                            uint32_t d1, uint16_t d2, uint16_t d3,
                            uint8_t d4, uint8_t d5,
                            uint8_t d6, uint8_t d7, uint8_t d8,
                            uint8_t d9, uint8_t d10, uint8_t d11) {
    out[0] = (uint8_t)(d1 >> 24);
    out[1] = (uint8_t)(d1 >> 16);
    out[2] = (uint8_t)(d1 >> 8);
    out[3] = (uint8_t)(d1);
    out[4] = (uint8_t)(d2 >> 8);
    out[5] = (uint8_t)(d2);
    out[6] = (uint8_t)(d3 >> 8);
    out[7] = (uint8_t)(d3);
    out[8] = d4;
    out[9] = d5;
    out[10] = d6;
    out[11] = d7;
    out[12] = d8;
    out[13] = d9;
    out[14] = d10;
    out[15] = d11;
}

static void utf8_to_utf16le(const char *s, uint16_t *out, size_t max_chars) {
    size_t i = 0;
    while (*s && i + 1 < max_chars) {
        out[i++] = (uint16_t)(uint8_t)*s++;
    }
    if (i < max_chars) out[i] = 0;
}

__attribute__((visibility("default"))) int disk_sector0_is_fat(const uint8_t *sec) {
    if (!sec) return 0;
    if (sec[510] != 0x55 || sec[511] != 0xAA) return 0;
    if (sec[82] == 'F' && sec[83] == 'A' && sec[84] == 'T') return 1;
    if (sec[54] == 'F' && sec[55] == 'A' && sec[56] == 'T') return 1;
    return 0;
}

/* Workspace in .bss — keep GPT I/O off PID1 stack (installer is stack-heavy). */
static uint8_t       g_gpt_sec[512];
static uint8_t       g_gpt_hdr[512];
static mbr_t         g_gpt_pmbr;
static gpt_header_t  g_gpt_ph;
static gpt_header_t  g_gpt_bh;
static gpt_part_t    g_gpt_parts[128];

int gpt_init_single_disk(block_dev_t *dev) {
    if (!dev || !dev->write || !dev->read || dev->sector_size != 512)
        return -1;
    if (dev->sector_count < (34ULL + ESP_SIZE_SECTORS + 1024ULL))
        return -1;

    if (dev->read(dev, 0, 1, g_gpt_sec) != 0)
        return -1;
    uint16_t sig = (uint16_t)g_gpt_sec[510] | ((uint16_t)g_gpt_sec[511] << 8);
    if (sig == 0xAA55) {
        if (disk_sector0_is_fat(g_gpt_sec))
            return 0;
        if (dev->read(dev, 1, 1, g_gpt_hdr) == 0) {
            if (memcmp(g_gpt_hdr, "EFI PART", 8) == 0 &&
                dev->sector_count >= (34ULL + ESP_SIZE_SECTORS + 1024ULL)) {
                return 0;
            }
        }
    }

    uint64_t total = dev->sector_count;
    uint64_t first_usable = 2048;
    uint64_t esp_first = first_usable;
    uint64_t esp_last = esp_first + ESP_SIZE_SECTORS - 1;
    uint64_t root_first = esp_last + 1;
    uint64_t last_usable = total - 34;
    uint64_t root_last = last_usable;
    uint64_t part_entries_lba = 2;
    uint64_t backup_part_entries_lba = total - 33;
    uint64_t primary_hdr_lba = 1;
    uint64_t backup_hdr_lba = total - 1;

    memset(&g_gpt_pmbr, 0, sizeof(g_gpt_pmbr));
    g_gpt_pmbr.part[0].type = 0xEE;
    g_gpt_pmbr.part[0].lba_first = 1;
    g_gpt_pmbr.part[0].lba_count =
        (total > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : (uint32_t)total;
    g_gpt_pmbr.sig = 0xAA55;
    if (dev->write(dev, 0, 1, &g_gpt_pmbr) != 0)
        return -1;

    memset(g_gpt_parts, 0, sizeof(g_gpt_parts));

    uint8_t esp_type[16];
    guid_from_bytes(esp_type,
                    0xC12A7328U, 0xF81FU, 0x11D2U,
                    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B);
    uint8_t root_type[16];
    guid_from_bytes(root_type,
                    0x0FC63DAFU, 0x8483U, 0x4772U,
                    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4);

    memcpy(g_gpt_parts[0].type_guid, esp_type, 16);
    memcpy(g_gpt_parts[0].part_guid, esp_type, 16);
    g_gpt_parts[0].first_lba = esp_first;
    g_gpt_parts[0].last_lba  = esp_last;
    utf8_to_utf16le("ESP", g_gpt_parts[0].name, 36);

    memcpy(g_gpt_parts[1].type_guid, root_type, 16);
    memcpy(g_gpt_parts[1].part_guid, root_type, 16);
    g_gpt_parts[1].first_lba = root_first;
    g_gpt_parts[1].last_lba  = root_last;
    utf8_to_utf16le("ASD root", g_gpt_parts[1].name, 36);

    memset(&g_gpt_ph, 0, sizeof(g_gpt_ph));
    memcpy(g_gpt_ph.signature, "EFI PART", 8);
    g_gpt_ph.revision = 0x00010000U;
    g_gpt_ph.header_size = 92; /* UEFI GPT header size (not sizeof struct) */
    g_gpt_ph.current_lba = primary_hdr_lba;
    g_gpt_ph.backup_lba = backup_hdr_lba;
    g_gpt_ph.first_usable_lba = first_usable;
    g_gpt_ph.last_usable_lba = last_usable;
    memset(g_gpt_ph.disk_guid, 0x11, sizeof(g_gpt_ph.disk_guid));
    g_gpt_ph.part_entry_lba = part_entries_lba;
    g_gpt_ph.num_parts = 128;
    g_gpt_ph.part_entry_size = (uint32_t)sizeof(gpt_part_t);
    g_gpt_ph.part_array_crc32 =
        crc32_compute(g_gpt_parts, sizeof(gpt_part_t) * 128);
    g_gpt_ph.header_crc32 = 0;
    g_gpt_ph.header_crc32 = crc32_compute(&g_gpt_ph, g_gpt_ph.header_size);

    g_gpt_bh = g_gpt_ph;
    g_gpt_bh.current_lba = backup_hdr_lba;
    g_gpt_bh.backup_lba  = primary_hdr_lba;
    g_gpt_bh.part_entry_lba = backup_part_entries_lba;
    g_gpt_bh.header_crc32 = 0;
    g_gpt_bh.header_crc32 = crc32_compute(&g_gpt_bh, g_gpt_bh.header_size);

    /* Write partition entries one sector at a time to keep I/O simple. */
    const uint8_t *pbytes = (const uint8_t *)g_gpt_parts;
    for (uint64_t i = 0; i < 32; i++) {
        if (dev->write(dev, part_entries_lba + i, 1,
                       (const void *)(pbytes + i * 512)) != 0) {
            return -1;
        }
    }
    if (dev->write(dev, primary_hdr_lba, 1, &g_gpt_ph) != 0) {
        return -1;
    }
    /* Write backup GPT structures at end-of-disk for correctness. */
    for (uint64_t i = 0; i < 32; i++) {
        if (dev->write(dev, backup_part_entries_lba + i, 1,
                       (const void *)(pbytes + i * 512)) != 0) {
            return -1;
        }
    }
    if (dev->write(dev, backup_hdr_lba, 1, &g_gpt_bh) != 0) {
        return -1;
    }

    return 0;
}

