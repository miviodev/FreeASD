/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * uname — print system information.
 *
 * Usage: uname [-a] [-s] [-n] [-r] [-v] [-m]
 *   -a  print all fields
 *   -s  sysname  (default)
 *   -n  nodename
 *   -r  release
 *   -v  version
 *   -m  machine
 */
#include "common.h"

int main(int argc, const char **argv) {
    asd_utsname_t u;
    memset(&u, 0, sizeof(u));
    asd_uname(&u);

    /* Parse flags */
    int flag_s = 0, flag_n = 0, flag_r = 0, flag_v = 0, flag_m = 0;
    int any_flag = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (arg[0] == '-') {
            for (int j = 1; arg[j]; j++) {
                switch (arg[j]) {
                case 'a': flag_s = flag_n = flag_r = flag_v = flag_m = 1; any_flag = 1; break;
                case 's': flag_s = 1; any_flag = 1; break;
                case 'n': flag_n = 1; any_flag = 1; break;
                case 'r': flag_r = 1; any_flag = 1; break;
                case 'v': flag_v = 1; any_flag = 1; break;
                case 'm': flag_m = 1; any_flag = 1; break;
                default:
                    printf("uname: unknown option '-%c'\n", arg[j]);
                    return 1;
                }
            }
        }
    }

    /* Default: print sysname only */
    if (!any_flag) flag_s = 1;

    int first = 1;
#define PRINT_FIELD(flag, val) \
    if (flag) { \
        if (!first) printf(" "); \
        printf("%s", val); \
        first = 0; \
    }

    PRINT_FIELD(flag_s, u.sysname);
    PRINT_FIELD(flag_n, u.nodename);
    PRINT_FIELD(flag_r, u.release);
    PRINT_FIELD(flag_v, u.version);
    PRINT_FIELD(flag_m, u.machine);

#undef PRINT_FIELD
    printf("\n");
    return 0;
}
