/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * hexdump — display file contents in hexadecimal.
 *
 * Usage: hexdump <file>
 *
 * Output format (same as hexdump -C on Linux):
 *   00000000  48 65 6c 6c 6f  |Hello|
 */
#include "common.h"

#define BUF_SIZE  4096
#define ROW_BYTES 16

static char hex_nibble(uint8_t v) {
    return v < 10 ? '0' + v : 'a' + v - 10;
}

static void print_hex_byte(uint8_t b) {
    putc(hex_nibble(b >> 4));
    putc(hex_nibble(b & 0xF));
}

static void print_hex_offset(uint64_t off) {
    for (int i = 28; i >= 0; i -= 4)
        putc(hex_nibble((off >> i) & 0xF));
}

int main(int argc, const char **argv) {
    if (argc < 2) {
        puts("usage: hexdump <file>");
        return 1;
    }

    int fd = asd_open(argv[1], O_RDONLY);
    if (fd < 0) {
        printf("hexdump: cannot open '%s'\n", argv[1]);
        return 1;
    }

    static uint8_t buf[BUF_SIZE];
    uint64_t offset = 0;
    long n;

    while ((n = asd_read(fd, buf, BUF_SIZE)) > 0) {
        long i = 0;
        while (i < n) {
            /* Print offset */
            print_hex_offset(offset + (uint64_t)i);
            printf("  ");

            /* Print hex bytes */
            long row_end = i + ROW_BYTES;
            if (row_end > n) row_end = n;
            for (long j = i; j < row_end; j++) {
                print_hex_byte(buf[j]);
                putc(' ');
                if (j - i == 7) putc(' ');  /* extra space in middle */
            }
            /* Pad if row is short */
            long row_len = row_end - i;
            for (long j = row_len; j < ROW_BYTES; j++) {
                printf("   ");
                if (j == 7) putc(' ');
            }

            /* Print ASCII representation */
            printf(" |");
            for (long j = i; j < row_end; j++) {
                uint8_t c = buf[j];
                putc((c >= 0x20 && c < 0x7F) ? (char)c : '.');
            }
            printf("|\n");

            i = row_end;
        }
        offset += (uint64_t)n;
    }

    /* Print final offset */
    print_hex_offset(offset);
    printf("\n");

    asd_close(fd);
    return 0;
}
