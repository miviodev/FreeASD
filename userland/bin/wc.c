/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * wc — word, line, and byte count.
 *
 * Usage: wc [-l] [-w] [-c] <file>
 *   -l  count lines
 *   -w  count words
 *   -c  count bytes
 *   (default: -l -w -c)
 */
#include "common.h"

#define BUF_SIZE 4096

int main(int argc, const char **argv) {
    int flag_l = 0, flag_w = 0, flag_c = 0;
    const char *filename = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                case 'l': flag_l = 1; break;
                case 'w': flag_w = 1; break;
                case 'c': flag_c = 1; break;
                default:
                    printf("wc: unknown option '-%c'\n", argv[i][j]);
                    return 1;
                }
            }
        } else {
            filename = argv[i];
        }
    }

    /* Default: all */
    if (!flag_l && !flag_w && !flag_c) flag_l = flag_w = flag_c = 1;

    if (!filename) {
        puts("usage: wc [-lwc] <file>");
        return 1;
    }

    int fd = asd_open(filename, O_RDONLY);
    if (fd < 0) {
        printf("wc: cannot open '%s'\n", filename);
        return 1;
    }

    static uint8_t buf[BUF_SIZE];
    uint64_t lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    long n;

    while ((n = asd_read(fd, buf, BUF_SIZE)) > 0) {
        bytes += (uint64_t)n;
        for (long i = 0; i < n; i++) {
            uint8_t c = buf[i];
            if (c == '\n') lines++;
            int is_space = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
            if (!is_space && !in_word) { words++; in_word = 1; }
            else if (is_space)          in_word = 0;
        }
    }

    asd_close(fd);

    if (flag_l) printf(" %lld", (long long)lines);
    if (flag_w) printf(" %lld", (long long)words);
    if (flag_c) printf(" %lld", (long long)bytes);
    printf(" %s\n", filename);
    return 0;
}
