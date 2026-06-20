/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * whoami — print effective user name.
 */
#include "common.h"

int main(int argc, const char **argv) {
    (void)argc; (void)argv;
    unsigned int euid = asd_geteuid();
    if (euid == 0)
        puts("root");
    else
        puts("user");
    return 0;
}
