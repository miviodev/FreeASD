/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 */

#ifndef ASD_USR_H
#define ASD_USR_H

#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------------- */
/* Well-known UIDs                                                         */
/* ---------------------------------------------------------------------- */

#define UID_ROOT        0
#define UID_SYSTEM_MIN  1
#define UID_SYSTEM_MAX  99
#define UID_USER_MIN    1000
#define UID_USER_MAX    65534
#define UID_NOBODY      65535

/* ---------------------------------------------------------------------- */
/* Well-known GIDs                                                         */
/* ---------------------------------------------------------------------- */

#define GID_ROOT        0
#define GID_WHEEL       1    /* privileged group — members may use doas    */
#define GID_USERS       100  /* default group for regular user accounts    */

/* ---------------------------------------------------------------------- */
/* Limits                                                                  */
/* ---------------------------------------------------------------------- */

#define USR_MAX_USERS   64
#define USR_MAX_GROUPS  32   /* bitmask scheme: max 32 groups per process  */
#define USR_NAME_LEN    32
#define USR_HOME_LEN    64
#define USR_SHELL_LEN   64

/* ---------------------------------------------------------------------- */
/* Types                                                                   */
/* ---------------------------------------------------------------------- */

typedef uint16_t uid_t;
typedef uint16_t gid_t;

/* ---------------------------------------------------------------------- */
/* User flags (usr_add flags parameter)                                    */
/* ---------------------------------------------------------------------- */

#define USR_FLAG_WHEEL    (1u << 0)  /* member of wheel; may gain root     */
#define USR_FLAG_NOLOGIN  (1u << 1)  /* service account; no interactive login */
#define USR_FLAG_LOCKED   (1u << 2)  /* account is disabled                */

/* ---------------------------------------------------------------------- */
/* Structures                                                              */
/* ---------------------------------------------------------------------- */

typedef struct {
    uid_t    uid;
    gid_t    gid;               /* primary group                           */
    uint32_t flags;             /* USR_FLAG_*                              */
    uint64_t pw_hash;           /* Salted, iterated hash of password       */
    uint64_t pw_salt;           /* Random 64-bit salt                      */
    char     name[USR_NAME_LEN];
    char     home[USR_HOME_LEN];
    char     shell[USR_SHELL_LEN];
    uint32_t groups;            /* bitmask: bit N = member of gid N        */
} asd_user_t;

typedef struct {
    gid_t    gid;
    char     name[USR_NAME_LEN];
    uint32_t member_mask;       /* bitmask: bit N = uid N is a member      */
} asd_group_t;

/* ---------------------------------------------------------------------- */
/* File permission mode bits (9-bit Unix style)                            */
/* ---------------------------------------------------------------------- */

#define USR_MODE_RUSR   0400u   /* owner read                              */
#define USR_MODE_WUSR   0200u   /* owner write                             */
#define USR_MODE_XUSR   0100u   /* owner execute                           */
#define USR_MODE_RGRP   0040u   /* group read                              */
#define USR_MODE_WGRP   0020u   /* group write                             */
#define USR_MODE_XGRP   0010u   /* group execute                           */
#define USR_MODE_ROTH   0004u   /* others read                             */
#define USR_MODE_WOTH   0002u   /* others write                            */
#define USR_MODE_XOTH   0001u   /* others execute                          */

#define USR_MODE_DIR_DEFAULT    0755u   /* rwxr-xr-x                       */
#define USR_MODE_FILE_DEFAULT   0644u   /* rw-r--r--                       */
#define USR_MODE_EXEC_DEFAULT   0755u   /* rwxr-xr-x                       */
#define USR_MODE_PRIV_DEFAULT   0600u   /* rw------- (root-only files)     */

/* Access flags for usr_check_perm */
#define USR_ACCESS_READ   1
#define USR_ACCESS_WRITE  2
#define USR_ACCESS_EXEC   4

/* ---------------------------------------------------------------------- */
/* API                                                                     */
/* ---------------------------------------------------------------------- */

/* Initialise the user/group database with built-in accounts (root, wheel,
   users groups).  Call once from kernel_main before asdinit. */
void usr_init(void);

/* Load users from an /etc/passwd-style text buffer.  Each line:
     name uid gid flags home shell [pw_hash_hex]
   where flags is "wheel", "nologin", "locked", or "-" for none.
   Optional 7th field is 16 hex digits of the FNV-1a-64 password hash. */
int usr_load_passwd(const char *buf, size_t len);

/* Serialise all users to buf as the same text format (with hash field).
   Returns number of bytes written, or -1 on error. */
int usr_write_passwd(char *buf, size_t buf_sz);

/* Load groups from an /etc/group-style text buffer.  Each line:
     name gid */
int usr_load_group(const char *buf, size_t len);

/* User lookup — returns NULL if not found */
asd_user_t  *usr_find_by_uid(uid_t uid);
asd_user_t  *usr_find_by_name(const char *name);

/* Add a user; returns 0 on success, -1 if table full or uid/name taken */
int usr_add(uid_t uid, gid_t gid, const char *name,
            const char *password, uint32_t flags,
            const char *home, const char *shell);

/* Set supplementary group membership */
int usr_add_to_group(uid_t uid, gid_t gid);
int usr_remove_from_group(uid_t uid, gid_t gid);

/* Returns 1 if uid holds wheel (or is root), 0 otherwise */
int usr_is_wheel(uid_t uid);

/* Returns 1 if password matches stored hash, 0 otherwise */
int usr_check_password(uid_t uid, const char *password);

/* Update user credentials (salt and hash) directly.
   Returns 0 on success, -1 if user not found. */
int usr_set_password(uid_t uid, const char *password);

/* Group lookup */
asd_group_t *grp_find_by_gid(gid_t gid);
asd_group_t *grp_find_by_name(const char *name);

/* Add a group; returns 0 on success, -1 if table full or gid/name taken */
int grp_add(gid_t gid, const char *name);

/* Enumerate users/groups by index (0-based); returns NULL past end */
uint32_t     usr_count(void);
asd_user_t  *usr_get(uint32_t index);
uint32_t     grp_count(void);
asd_group_t *grp_get(uint32_t index);

/* Check whether a process (uid, primary gid, groups bitmask) may perform
   access_flags on a file with the given owner/mode.
   Returns 0 if allowed, -1 if denied. */
int usr_check_perm(uid_t uid, gid_t gid, uint32_t groups,
                   uid_t file_uid, gid_t file_gid, uint16_t file_mode,
                   int access_flags);

#endif /* ASD_USR_H */
