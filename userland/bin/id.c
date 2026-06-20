/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * id — print real and effective user/group IDs.
 *
 * Output: uid=0(root) gid=0(root) euid=0(root) egid=0(root)
 */
#include "common.h"

static const char *uid_to_name(unsigned int uid) {
    if (uid == 0) return "root";
    return "user";
}

int main(int argc, const char **argv) {
    (void)argc; (void)argv;

    unsigned int uid  = asd_getuid();
    unsigned int gid  = asd_getgid();
    unsigned int euid = asd_geteuid();
    unsigned int egid = asd_getegid();

    printf("uid=%u(%s) gid=%u(%s) euid=%u(%s) egid=%u(%s)\n",
           uid,  uid_to_name(uid),
           gid,  uid_to_name(gid),
           euid, uid_to_name(euid),
           egid, uid_to_name(egid));
    return 0;
}
