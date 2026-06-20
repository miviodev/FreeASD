/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * OpenASD Security Module (ASM) — implementation.
 *
 * This module provides:
 *
 *  1. Stack canary — __stack_chk_guard is seeded from the entropy pool
 *     at boot.  __stack_chk_fail() triggers a kernel panic and halts.
 *
 *  2. Entropy pool — a 256-byte Fortuna-inspired pool seeded from:
 *       - TSC (rdtsc) at boot
 *       - RDRAND if the CPU supports it (CPUID leaf 1, ECX bit 30)
 *       - CPUID vendor/model strings
 *     The pool is mixed with a ChaCha20-inspired quarter-round.
 *
 *  3. Audit log — a 64-entry ring buffer recording security events.
 *     Each entry stores: timestamp (PIT ticks), pid, uid, event code,
 *     and a 48-character detail string.
 *
 *  4. Authentication rate limiter — tracks failed login attempts per
 *     UID.  After 5 failures within 60 seconds the account is locked
 *     for 30 seconds.
 *
 *  5. W^X policy check — rejects memory regions with both WRITE and
 *     EXECUTE permissions set simultaneously.
 */

#include "security.h"
#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Freestanding helpers                                                 */
/* ------------------------------------------------------------------ */

static void sec_memset(void *p, int v, size_t n) {
    volatile uint8_t *d = (volatile uint8_t *)p;
    while (n--) *d++ = (uint8_t)v;
}

static void sec_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

static size_t sec_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void sec_strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    while (i < n - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* CPU helpers                                                          */
/* ------------------------------------------------------------------ */

static uint64_t sec_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static int sec_rdrand64(uint64_t *out) {
    /* Check RDRAND support: CPUID leaf 1, ECX bit 30 */
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1), "c"(0));
    if (!(ecx & (1u << 30))) return 0;

    /* Try RDRAND up to 10 times (may fail under heavy load) */
    for (int i = 0; i < 10; i++) {
        uint64_t val;
        uint8_t  ok;
        __asm__ volatile(
            "rdrand %0\n\t"
            "setc   %1\n\t"
            : "=r"(val), "=qm"(ok)
        );
        if (ok) { *out = val; return 1; }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Entropy pool                                                         */
/* ------------------------------------------------------------------ */

#define POOL_WORDS 32   /* 256 bytes */
static uint64_t g_pool[POOL_WORDS];
static uint32_t g_pool_idx = 0;
static uint32_t g_pool_lock = 0;

static inline void pool_lock(void) {
    while (__atomic_exchange_n(&g_pool_lock, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}
static inline void pool_unlock(void) {
    __atomic_store_n(&g_pool_lock, 0, __ATOMIC_RELEASE);
}

/*
 * ChaCha20-inspired quarter round — used to mix the pool.
 * Not full ChaCha20; adapted for 64-bit words.
 */
#define ROTL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))
static void pool_quarter_round(uint64_t *a, uint64_t *b,
                                uint64_t *c, uint64_t *d) {
    *a += *b; *d ^= *a; *d = ROTL64(*d, 32);
    *c += *d; *b ^= *c; *b = ROTL64(*b, 24);
    *a += *b; *d ^= *a; *d = ROTL64(*d, 16);
    *c += *d; *b ^= *c; *b = ROTL64(*b, 63);
}

static void pool_mix(void) {
    /* Apply 4 quarter rounds across the pool */
    for (int i = 0; i < 4; i++) {
        pool_quarter_round(&g_pool[0 + i], &g_pool[4 + i],
                           &g_pool[8 + i], &g_pool[12 + i]);
        pool_quarter_round(&g_pool[16 + i], &g_pool[20 + i],
                           &g_pool[24 + i], &g_pool[28 + i]);
    }
}

void entropy_mix(uint64_t seed) {
    pool_lock();
    g_pool[g_pool_idx % POOL_WORDS] ^= seed;
    g_pool_idx++;
    pool_mix();
    pool_unlock();
}

uint64_t entropy_get64(void) {
    pool_lock();
    /* Mix in current TSC to add freshness */
    g_pool[g_pool_idx % POOL_WORDS] ^= sec_rdtsc();
    pool_mix();
    uint64_t result = g_pool[0] ^ g_pool[POOL_WORDS / 2];
    /* Advance pool state */
    g_pool[0] += g_pool[1];
    g_pool_idx++;
    pool_unlock();
    return result;
}

/* ------------------------------------------------------------------ */
/* Stack canary                                                         */
/* ------------------------------------------------------------------ */

uint64_t __stack_chk_guard = 0;

extern void serial_puts(const char *s);

__attribute__((noreturn, no_stack_protector))
void __stack_chk_fail(void) {
    /* Log the event before panicking */
    audit_log(0, 0, AUDIT_STACK_SMASH,
              "stack smashing detected — halting");
    serial_puts("\n*** KERNEL PANIC: STACK SMASH DETECTED ***\n");
    for (;;) __asm__ volatile("hlt");
}

/* ------------------------------------------------------------------ */
/* Audit log                                                            */
/* ------------------------------------------------------------------ */

#define AUDIT_RING_SIZE  64
#define AUDIT_DETAIL_LEN 48

typedef struct {
    uint64_t timestamp_ns;
    uint32_t pid;
    uint32_t uid;
    uint8_t  event;
    char     detail[AUDIT_DETAIL_LEN];
} audit_entry_t;

static audit_entry_t g_audit_ring[AUDIT_RING_SIZE];
static uint32_t      g_audit_head = 0;
static uint32_t      g_audit_lock = 0;

static inline void audit_lock(void) {
    while (__atomic_exchange_n(&g_audit_lock, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}
static inline void audit_unlock(void) {
    __atomic_store_n(&g_audit_lock, 0, __ATOMIC_RELEASE);
}

extern uint64_t pit_ticks(void);

void audit_log(uint32_t pid, uint32_t uid, uint8_t event,
               const char *detail) {
    audit_lock();
    audit_entry_t *e = &g_audit_ring[g_audit_head % AUDIT_RING_SIZE];
    e->timestamp_ns = pit_ticks() * 10000000ULL;
    e->pid   = pid;
    e->uid   = uid;
    e->event = event;
    sec_memset(e->detail, 0, AUDIT_DETAIL_LEN);
    if (detail) {
        size_t len = sec_strlen(detail);
        if (len >= AUDIT_DETAIL_LEN) len = AUDIT_DETAIL_LEN - 1;
        sec_memcpy(e->detail, detail, len);
    }
    g_audit_head++;
    audit_unlock();
}

extern void put_u64(uint64_t n);

void audit_dump(void) {
    audit_lock();
    uint32_t count = (g_audit_head < AUDIT_RING_SIZE)
                   ? g_audit_head : AUDIT_RING_SIZE;
    uint32_t start = (g_audit_head >= AUDIT_RING_SIZE)
                   ? g_audit_head % AUDIT_RING_SIZE : 0;
    for (uint32_t i = 0; i < count; i++) {
        audit_entry_t *e = &g_audit_ring[(start + i) % AUDIT_RING_SIZE];
        serial_puts("[AUDIT] ts=");
        put_u64(e->timestamp_ns);
        serial_puts(" pid=");
        put_u64(e->pid);
        serial_puts(" uid=");
        put_u64(e->uid);
        serial_puts(" ev=");
        put_u64(e->event);
        serial_puts(" ");
        serial_puts(e->detail);
        serial_puts("\n");
    }
    audit_unlock();
}

/* ------------------------------------------------------------------ */
/* Authentication rate limiter                                          */
/* ------------------------------------------------------------------ */

#define RATELIMIT_MAX_USERS  32
#define RATELIMIT_WINDOW_NS  (60ULL * 1000000000ULL)   /* 60 seconds */
#define RATELIMIT_LOCKOUT_NS (30ULL * 1000000000ULL)   /* 30 seconds */
#define RATELIMIT_THRESHOLD  5

typedef struct {
    uint32_t uid;
    uint32_t fail_count;
    uint64_t first_fail_ns;
    uint64_t lockout_until_ns;
} ratelimit_entry_t;

static ratelimit_entry_t g_rl_table[RATELIMIT_MAX_USERS];
static uint32_t          g_rl_lock = 0;

static inline void rl_lock(void) {
    while (__atomic_exchange_n(&g_rl_lock, 1, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");
}
static inline void rl_unlock(void) {
    __atomic_store_n(&g_rl_lock, 0, __ATOMIC_RELEASE);
}

static ratelimit_entry_t *rl_find(uint32_t uid) {
    for (int i = 0; i < RATELIMIT_MAX_USERS; i++) {
        if (g_rl_table[i].uid == uid && g_rl_table[i].fail_count > 0)
            return &g_rl_table[i];
    }
    return NULL;
}

static ratelimit_entry_t *rl_alloc(uint32_t uid) {
    /* Find empty slot */
    for (int i = 0; i < RATELIMIT_MAX_USERS; i++) {
        if (g_rl_table[i].fail_count == 0) {
            g_rl_table[i].uid = uid;
            return &g_rl_table[i];
        }
    }
    /* Evict oldest entry */
    uint64_t oldest_ts = (uint64_t)-1ULL;
    int oldest_idx = 0;
    for (int i = 0; i < RATELIMIT_MAX_USERS; i++) {
        if (g_rl_table[i].first_fail_ns < oldest_ts) {
            oldest_ts  = g_rl_table[i].first_fail_ns;
            oldest_idx = i;
        }
    }
    sec_memset(&g_rl_table[oldest_idx], 0, sizeof(ratelimit_entry_t));
    g_rl_table[oldest_idx].uid = uid;
    return &g_rl_table[oldest_idx];
}

int auth_fail(uint32_t uid) {
    uint64_t now_ns = pit_ticks() * 10000000ULL;
    rl_lock();

    ratelimit_entry_t *e = rl_find(uid);
    if (!e) e = rl_alloc(uid);

    /* Check if still in lockout */
    if (e->lockout_until_ns > now_ns) {
        rl_unlock();
        audit_log(0, uid, AUDIT_LOGIN_FAIL,
                  "login attempt during lockout");
        return 1;
    }

    /* Reset window if expired */
    if (now_ns - e->first_fail_ns > RATELIMIT_WINDOW_NS) {
        e->fail_count    = 0;
        e->first_fail_ns = now_ns;
    }

    if (e->fail_count == 0) e->first_fail_ns = now_ns;
    e->fail_count++;

    int locked = 0;
    if (e->fail_count >= RATELIMIT_THRESHOLD) {
        e->lockout_until_ns = now_ns + RATELIMIT_LOCKOUT_NS;
        locked = 1;
        audit_log(0, uid, AUDIT_LOGIN_FAIL,
                  "account locked after repeated failures");
    } else {
        audit_log(0, uid, AUDIT_LOGIN_FAIL, "bad password");
    }

    rl_unlock();
    return locked;
}

void auth_success(uint32_t uid) {
    rl_lock();
    ratelimit_entry_t *e = rl_find(uid);
    if (e) sec_memset(e, 0, sizeof(*e));
    rl_unlock();
    audit_log(0, uid, AUDIT_LOGIN_OK, "login successful");
}

/* ------------------------------------------------------------------ */
/* W^X policy                                                           */
/* ------------------------------------------------------------------ */

int security_check_wx(int prot) {
    if ((prot & SEC_PROT_WRITE) && (prot & SEC_PROT_EXEC)) {
        audit_log(0, 0, AUDIT_SYSCALL_DENY,
                  "W^X violation: write+exec mapping rejected");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Initialisation                                                       */
/* ------------------------------------------------------------------ */

__attribute__((no_stack_protector))
void security_init(void) {
    sec_memset(g_pool,      0, sizeof(g_pool));
    sec_memset(g_audit_ring, 0, sizeof(g_audit_ring));
    sec_memset(g_rl_table,   0, sizeof(g_rl_table));

    /* Seed entropy pool from TSC */
    uint64_t tsc = sec_rdtsc();
    entropy_mix(tsc);
    entropy_mix(tsc ^ 0xdeadbeefcafe0000ULL);

    /* Seed from RDRAND if available */
    uint64_t rnd = 0;
    if (sec_rdrand64(&rnd)) {
        entropy_mix(rnd);
    }

    /* Seed from CPUID vendor string */
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0), "c"(0));
    entropy_mix(((uint64_t)ebx << 32) | edx);
    entropy_mix(((uint64_t)ecx << 32) | eax);

    /* Seed from CPUID processor serial / model */
    __asm__ volatile("cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1), "c"(0));
    entropy_mix(((uint64_t)eax << 32) | edx);

    /* Mix in another TSC reading for additional freshness */
    entropy_mix(sec_rdtsc());

    /* Set stack canary — ensure it is never 0 or all-ones */
    uint64_t canary = entropy_get64();
    if (canary == 0 || canary == (uint64_t)-1ULL)
        canary = 0x596f75416c6c6f77ULL;  /* "YouAllow" as fallback */
    __stack_chk_guard = canary;

    audit_log(0, 0, AUDIT_EXEC, "security module initialised");
}
