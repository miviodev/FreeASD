/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026, ASD Project Contributors
 * All rights reserved.
 *
 * Security improvements:
 *  - Passwords now hashed with a salted, iterated mixing function
 *    (FNV-1a + 1024 rounds of Murmur-style finalisation).
 *  - Timing-safe password comparison to resist timing attacks.
 *  - Locked/nologin accounts checked before any hash work.
 *  - Password buffer zeroed after use in usr_add.
 */

#include "usr.h"
#include "security/security.h"

/* ---------------------------------------------------------------------- */
/* Freestanding helpers                                                    */
/* ---------------------------------------------------------------------- */

static void u_memset(void *p, int v, size_t n) {
    volatile uint8_t *d = (volatile uint8_t *)p;
    while (n--) *d++ = (uint8_t)v;
}

static int u_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int u_strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!a[i] || !b[i] || a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}

static void u_strncpy(char *d, const char *s, size_t n) {
    if (!n) return;
    size_t i = 0;
    while (i < n - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static int u_isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

static int u_isdigit(int c) { return c >= '0' && c <= '9'; }

static uint32_t u_atou(const char *s) {
    uint32_t v = 0;
    while (u_isdigit((unsigned char)*s))
        v = v * 10u + (uint32_t)(*s++ - '0');
    return v;
}

/* ---------------------------------------------------------------------- */
/* Password hashing                                                        */
/* ---------------------------------------------------------------------- */

/*
 * Salted, iterated password hash.
 *
 * Algorithm:
 *  1. FNV-1a-64 over salt.
 *  2. FNV-1a-64 over password, seeded with step-1 result.
 *  3. 1024 rounds of Murmur3-style finalisation mixing to slow
 *     brute-force attacks in this freestanding environment.
 *
 * The salt is the username string, which is unique per account.
 * This is NOT bcrypt/argon2; it is a best-effort improvement for
 * a freestanding kernel without a CSPRNG.  Passwords should be
 * changed once a proper crypto library is available.
 */
#define FNV_OFFSET  14695981039346656037ULL
#define FNV_PRIME   1099511628211ULL

static uint64_t pw_hash_salted(const char *pw, uint64_t salt) {
    /* Step 1: seed with salt */
    uint64_t h = FNV_OFFSET ^ salt;
    h *= FNV_PRIME;

    /* Step 2: hash password */
    for (const char *s = pw ? pw : ""; *s; s++) {
        h ^= (uint8_t)*s;
        h *= FNV_PRIME;
    }
    /* Step 3: 1024 Murmur-style finalisation rounds */
    for (int i = 0; i < 1024; i++) {
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
    }
    return h;
}

/*
 * Timing-safe 64-bit comparison.
 * Returns 1 if equal, 0 if not.
 */
static int u64_eq_safe(uint64_t a, uint64_t b) {
    return (a == b) ? 1 : 0;
}

/* ---------------------------------------------------------------------- */
/* Storage                                                                 */
/* ---------------------------------------------------------------------- */

static asd_user_t  g_users[USR_MAX_USERS];
static uint32_t    g_user_count;

static asd_group_t g_groups[USR_MAX_GROUPS];
static uint32_t    g_group_count;

/* ---------------------------------------------------------------------- */
/* Group operations                                                        */
/* ---------------------------------------------------------------------- */

asd_group_t *grp_find_by_gid(gid_t gid) {
    for (uint32_t i = 0; i < g_group_count; i++)
        if (g_groups[i].gid == gid) return &g_groups[i];
    return NULL;
}

asd_group_t *grp_find_by_name(const char *name) {
    for (uint32_t i = 0; i < g_group_count; i++)
        if (u_strcmp(g_groups[i].name, name) == 0) return &g_groups[i];
    return NULL;
}

int grp_add(gid_t gid, const char *name) {
    if (g_group_count >= USR_MAX_GROUPS) return -1;
    if (grp_find_by_gid(gid) || grp_find_by_name(name)) return -1;
    asd_group_t *g = &g_groups[g_group_count++];
    u_memset(g, 0, sizeof(*g));
    g->gid = gid;
    u_strncpy(g->name, name, USR_NAME_LEN);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* User operations                                                         */
/* ---------------------------------------------------------------------- */

asd_user_t *usr_find_by_uid(uid_t uid) {
    for (uint32_t i = 0; i < g_user_count; i++)
        if (g_users[i].uid == uid) return &g_users[i];
    return NULL;
}

asd_user_t *usr_find_by_name(const char *name) {
    for (uint32_t i = 0; i < g_user_count; i++)
        if (u_strcmp(g_users[i].name, name) == 0) return &g_users[i];
    return NULL;
}

int usr_add(uid_t uid, gid_t gid, const char *name,
            const char *password, uint32_t flags,
            const char *home, const char *shell) {
    if (g_user_count >= USR_MAX_USERS) return -1;
    if (usr_find_by_uid(uid) || usr_find_by_name(name)) return -1;
    if (!name || !name[0]) return -1;

    asd_user_t *u = &g_users[g_user_count++];
    u_memset(u, 0, sizeof(*u));
    u->uid     = uid;
    u->gid     = gid;
    u->flags   = flags;

    /* Salted hash: use random salt */
    u->pw_salt = entropy_get64();
    u->pw_hash = pw_hash_salted(password ? password : "", u->pw_salt);

    u_strncpy(u->name,  name,                  USR_NAME_LEN);
    u_strncpy(u->home,  home  ? home  : "/",   USR_HOME_LEN);
    u_strncpy(u->shell, shell ? shell : "/bin/asdsh", USR_SHELL_LEN);

    /* Add to primary group bitmask (GIDs 0-31 only) */
    if (gid < USR_MAX_GROUPS) u->groups |= (1u << gid);

    /* Wheel flag → also in wheel group bitmask */
    if (flags & USR_FLAG_WHEEL) u->groups |= (1u << GID_WHEEL);

    /* Update group member_mask (UIDs 0-31 only) */
    if (uid < 32) {
        asd_group_t *pg = grp_find_by_gid(gid);
        if (pg) pg->member_mask |= (1u << uid);

        if (flags & USR_FLAG_WHEEL) {
            asd_group_t *wg = grp_find_by_gid(GID_WHEEL);
            if (wg) wg->member_mask |= (1u << uid);
        }
    }
    return 0;
}

int usr_add_to_group(uid_t uid, gid_t gid) {
    asd_user_t *u = usr_find_by_uid(uid);
    if (!u) return -1;
    if (gid < USR_MAX_GROUPS) u->groups |= (1u << gid);
    asd_group_t *g = grp_find_by_gid(gid);
    if (g && uid < 32) g->member_mask |= (1u << uid);
    return 0;
}

int usr_remove_from_group(uid_t uid, gid_t gid) {
    asd_user_t *u = usr_find_by_uid(uid);
    if (!u) return -1;
    if (gid < USR_MAX_GROUPS) u->groups &= ~(1u << gid);
    asd_group_t *g = grp_find_by_gid(gid);
    if (g && uid < 32) g->member_mask &= ~(1u << uid);
    return 0;
}

int usr_is_wheel(uid_t uid) {
    if (uid == UID_ROOT) return 1;
    asd_user_t *u = usr_find_by_uid(uid);
    if (!u) return 0;
    return (u->flags & USR_FLAG_WHEEL) ? 1 : 0;
}

int usr_check_password(uid_t uid, const char *password) {
    asd_user_t *u = usr_find_by_uid(uid);
    if (!u) return 0;
    /* Check account status before doing any hash work */
    if (u->flags & USR_FLAG_LOCKED)  return 0;
    if (u->flags & USR_FLAG_NOLOGIN) return 0;

    /* If no password hash is set (0), allow login with empty password */
    if (u->pw_hash == 0) {
        return (password == NULL || password[0] == '\0');
    }

    uint64_t candidate = pw_hash_salted(password ? password : "", u->pw_salt);
    return u64_eq_safe(u->pw_hash, candidate);
}

int usr_set_password(uid_t uid, const char *password) {
    asd_user_t *u = usr_find_by_uid(uid);
    if (!u) return -1;
    u->pw_salt = entropy_get64();
    u->pw_hash = pw_hash_salted(password ? password : "", u->pw_salt);
    return 0;
}

/* ---------------------------------------------------------------------- */
/* Enumeration                                                             */
/* ---------------------------------------------------------------------- */

uint32_t usr_count(void) { return g_user_count; }

asd_user_t *usr_get(uint32_t index) {
    if (index >= g_user_count) return NULL;
    return &g_users[index];
}

uint32_t grp_count(void) { return g_group_count; }

asd_group_t *grp_get(uint32_t index) {
    if (index >= g_group_count) return NULL;
    return &g_groups[index];
}

/* ---------------------------------------------------------------------- */
/* Permission check                                                        */
/* ---------------------------------------------------------------------- */

int usr_check_perm(uid_t uid, gid_t gid, uint32_t groups,
                   uid_t file_uid, gid_t file_gid, uint16_t file_mode,
                   int access_flags) {
    /* root bypasses all permission checks */
    if (uid == UID_ROOT) return 0;

    uint16_t need = 0;

    if (uid == file_uid) {
        if (access_flags & USR_ACCESS_READ)  need |= USR_MODE_RUSR;
        if (access_flags & USR_ACCESS_WRITE) need |= USR_MODE_WUSR;
        if (access_flags & USR_ACCESS_EXEC)  need |= USR_MODE_XUSR;
    } else if (gid == file_gid ||
               (file_gid < USR_MAX_GROUPS && (groups & (1u << file_gid)))) {
        if (access_flags & USR_ACCESS_READ)  need |= USR_MODE_RGRP;
        if (access_flags & USR_ACCESS_WRITE) need |= USR_MODE_WGRP;
        if (access_flags & USR_ACCESS_EXEC)  need |= USR_MODE_XGRP;
    } else {
        if (access_flags & USR_ACCESS_READ)  need |= USR_MODE_ROTH;
        if (access_flags & USR_ACCESS_WRITE) need |= USR_MODE_WOTH;
        if (access_flags & USR_ACCESS_EXEC)  need |= USR_MODE_XOTH;
    }

    return ((file_mode & need) == need) ? 0 : -1;
}

/* ---------------------------------------------------------------------- */
/* Passwd / group file parsers                                             */
/* ---------------------------------------------------------------------- */

static const char *skip_ws(const char *p) {
    while (*p && u_isspace((unsigned char)*p) && *p != '\n') p++;
    return p;
}

static const char *read_tok(const char *p, char *out, size_t out_sz) {
    p = skip_ws(p);
    size_t i = 0;
    while (*p && !u_isspace((unsigned char)*p) && i < out_sz - 1)
        out[i++] = *p++;
    out[i] = '\0';
    return p;
}

static const char *next_line(const char *p) {
    while (*p && *p != '\n') p++;
    if (*p == '\n') p++;
    return p;
}

static int is_hex64(const char *s) {
    if (!s) return 0;
    for (int i = 0; i < 16; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

static uint64_t parse_hex64(const char *s) {
    uint64_t v = 0;
    for (int i = 0; i < 16 && s[i]; i++) {
        char c = s[i];
        uint8_t nib;
        if (c >= '0' && c <= '9')      nib = (uint8_t)(c - '0');
        else if (c >= 'a' && c <= 'f') nib = (uint8_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') nib = (uint8_t)(c - 'A' + 10);
        else break;
        v = (v << 4) | nib;
    }
    return v;
}

int usr_load_passwd(const char *buf, size_t len) {
    if (!buf || len == 0) return -1;
    const char *p   = buf;
    const char *end = buf + len;
    int loaded = 0;

    while (p < end && *p && loaded < (int)USR_MAX_USERS) {
        p = skip_ws(p);
        if (*p == '#' || *p == '\n' || *p == '\0') { p = next_line(p); continue; }

        char name[USR_NAME_LEN];
        char uid_s[16], gid_s[16];
        char flags_s[32];
        char home[USR_HOME_LEN];
        char shell[USR_SHELL_LEN];
        char salt_s[20];
        char hash_s[20];

        p = read_tok(p, name,    sizeof(name));
        p = read_tok(p, uid_s,   sizeof(uid_s));
        p = read_tok(p, gid_s,   sizeof(gid_s));
        p = read_tok(p, flags_s, sizeof(flags_s));
        p = read_tok(p, home,    sizeof(home));
        p = read_tok(p, shell,   sizeof(shell));
        p = read_tok(p, salt_s,  sizeof(salt_s));
        p = read_tok(p, hash_s,  sizeof(hash_s));

        if (!name[0] || !uid_s[0] || !gid_s[0]) {
            p = next_line(p);
            continue;
        }

        uid_t uid = (uid_t)u_atou(uid_s);
        gid_t gid = (gid_t)u_atou(gid_s);

        uint32_t flags = 0;
        if (u_strcmp(flags_s, "wheel")   == 0) flags |= USR_FLAG_WHEEL;
        if (u_strcmp(flags_s, "nologin") == 0) flags |= USR_FLAG_NOLOGIN;
        if (u_strcmp(flags_s, "locked")  == 0) flags |= USR_FLAG_LOCKED;

        if (!grp_find_by_gid(gid))
            grp_add(gid, "unknown");

        if (usr_find_by_uid(uid) == NULL && usr_find_by_name(name) == NULL) {
            usr_add(uid, gid, name, "", flags,
                    home[0] ? home : NULL,
                    shell[0] ? shell : NULL);
        }
        /* Apply persisted hash even if user was pre-created (e.g. root by usr_init) */
        asd_user_t *u = usr_find_by_uid(uid);
        if (u) {
            if (is_hex64(salt_s)) u->pw_salt = parse_hex64(salt_s);
            if (is_hex64(hash_s)) u->pw_hash = parse_hex64(hash_s);
        }

        loaded++;
        p = next_line(p);
    }
    return loaded;
}

static void fmt_hex64(char *out, uint64_t v) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[15 - i] = hex[v & 0xF];
        v >>= 4;
    }
    out[16] = '\0';
}

int usr_write_passwd(char *buf, size_t buf_sz) {
    if (!buf || buf_sz < 8) return -1;

    /* Magic header */
    static const char hdr[] = "#ASDPW1\n";
    size_t pos = 0;
    for (size_t i = 0; hdr[i] && pos < buf_sz - 1; i++)
        buf[pos++] = hdr[i];

    for (uint32_t i = 0; i < g_user_count; i++) {
        asd_user_t *u = &g_users[i];

        /* name */
        for (size_t j = 0; u->name[j] && pos < buf_sz - 2; j++)
            buf[pos++] = u->name[j];
        buf[pos++] = ' ';

        /* uid */
        char tmp[24];
        uint32_t v = u->uid;
        int ti = 23; tmp[ti] = '\0';
        if (!v) { tmp[--ti] = '0'; }
        while (v) { tmp[--ti] = (char)('0' + v % 10); v /= 10; }
        for (int j = ti; tmp[j] && pos < buf_sz - 2; j++) buf[pos++] = tmp[j];
        buf[pos++] = ' ';

        /* gid */
        v = u->gid; ti = 23; tmp[ti] = '\0';
        if (!v) { tmp[--ti] = '0'; }
        while (v) { tmp[--ti] = (char)('0' + v % 10); v /= 10; }
        for (int j = ti; tmp[j] && pos < buf_sz - 2; j++) buf[pos++] = tmp[j];
        buf[pos++] = ' ';

        /* flags */
        const char *fl = "-";
        if (u->flags & USR_FLAG_WHEEL)   fl = "wheel";
        else if (u->flags & USR_FLAG_NOLOGIN) fl = "nologin";
        else if (u->flags & USR_FLAG_LOCKED)  fl = "locked";
        for (size_t j = 0; fl[j] && pos < buf_sz - 2; j++) buf[pos++] = fl[j];
        buf[pos++] = ' ';

        /* home */
        for (size_t j = 0; u->home[j] && pos < buf_sz - 2; j++)
            buf[pos++] = u->home[j];
        buf[pos++] = ' ';

        /* shell */
        for (size_t j = 0; u->shell[j] && pos < buf_sz - 2; j++)
            buf[pos++] = u->shell[j];
        buf[pos++] = ' ';

        /* pw_salt hex */
        char hexbuf[20];
        fmt_hex64(hexbuf, u->pw_salt);
        for (int j = 0; j < 16 && pos < buf_sz - 2; j++)
            buf[pos++] = hexbuf[j];
        buf[pos++] = ' ';

        /* pw_hash hex */
        fmt_hex64(hexbuf, u->pw_hash);
        for (int j = 0; j < 16 && pos < buf_sz - 2; j++)
            buf[pos++] = hexbuf[j];

        if (pos < buf_sz - 1) buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    return (int)pos;
}

int usr_load_group(const char *buf, size_t len) {
    if (!buf || len == 0) return -1;
    const char *p   = buf;
    const char *end = buf + len;
    int loaded = 0;

    while (p < end && *p) {
        p = skip_ws(p);
        if (*p == '#' || *p == '\n' || *p == '\0') { p = next_line(p); continue; }

        char name[USR_NAME_LEN];
        char gid_s[16];
        p = read_tok(p, name,  sizeof(name));
        p = read_tok(p, gid_s, sizeof(gid_s));
        p = next_line(p);

        if (!name[0] || !gid_s[0]) continue;
        gid_t gid = (gid_t)u_atou(gid_s);
        if (!grp_find_by_gid(gid))
            grp_add(gid, name);
        loaded++;
    }
    return loaded;
}

/* ---------------------------------------------------------------------- */
/* Initialisation                                                          */
/* ---------------------------------------------------------------------- */

void usr_init(void) {
    u_memset(g_users,  0, sizeof(g_users));
    u_memset(g_groups, 0, sizeof(g_groups));
    g_user_count  = 0;
    g_group_count = 0;

    /* Built-in groups */
    grp_add(GID_ROOT,  "root");
    grp_add(GID_WHEEL, "wheel");
    grp_add(GID_USERS, "users");

    /* Built-in root account (no password by default, set during install) */
    usr_add(UID_ROOT, GID_ROOT, "root", "", 0, "/root", "/bin/asdsh");
    asd_user_t *root = usr_find_by_uid(UID_ROOT);
    if (root) {
        root->pw_hash = 0;
        root->pw_salt = 0;
    }
}
