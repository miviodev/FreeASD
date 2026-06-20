/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * OpenASD Security Module (ASM)
 *
 * Provides:
 *  - Stack canary initialisation and per-process canary storage
 *  - Mandatory Access Control (MAC) stub for future policy enforcement
 *  - Audit log ring buffer for security-relevant events
 *  - Rate limiter for failed authentication attempts
 *  - Entropy pool seeded from TSC + RDRAND (if available)
 */
#ifndef ASD_SECURITY_H
#define ASD_SECURITY_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ */
/* Stack canary                                                         */
/* ------------------------------------------------------------------ */

/*
 * Global stack canary value.  Initialised once at boot from the
 * entropy pool.  GCC's -fstack-protector uses __stack_chk_guard.
 */
extern uint64_t __stack_chk_guard;

/* Called by _start before any C code runs */
void security_init(void);

/* GCC/Clang stack-protector failure handler */
__attribute__((noreturn)) void __stack_chk_fail(void);

/* ------------------------------------------------------------------ */
/* Entropy pool                                                         */
/* ------------------------------------------------------------------ */

/*
 * Mix entropy into the pool from a 64-bit seed value.
 * Called at boot with TSC, CPUID, and RDRAND output.
 */
void entropy_mix(uint64_t seed);

/*
 * Extract a 64-bit pseudo-random value from the pool.
 * NOT cryptographically secure; suitable for canaries and salts.
 */
uint64_t entropy_get64(void);

/* ------------------------------------------------------------------ */
/* Audit log                                                            */
/* ------------------------------------------------------------------ */

#define AUDIT_LOGIN_OK     0x01
#define AUDIT_LOGIN_FAIL   0x02
#define AUDIT_PRIV_ESCALATE 0x03
#define AUDIT_SYSCALL_DENY 0x04
#define AUDIT_EXEC         0x05
#define AUDIT_STACK_SMASH  0x06

/*
 * Record a security event in the kernel audit ring buffer.
 * pid  — process ID (0 = kernel)
 * uid  — user ID
 * event — one of AUDIT_* constants above
 * detail — short description string (truncated to 48 chars)
 */
void audit_log(uint32_t pid, uint32_t uid, uint8_t event,
               const char *detail);

/*
 * Dump the audit log to the kernel console (for debugging).
 */
void audit_dump(void);

/* ------------------------------------------------------------------ */
/* Authentication rate limiter                                          */
/* ------------------------------------------------------------------ */

/*
 * Record a failed login attempt for uid.
 * Returns 1 if the account should be temporarily locked (>= 5 failures
 * within the last 60 seconds of PIT ticks), 0 otherwise.
 */
int auth_fail(uint32_t uid);

/*
 * Reset the failure counter for uid on successful login.
 */
void auth_success(uint32_t uid);

/* ------------------------------------------------------------------ */
/* W^X policy enforcement                                               */
/* ------------------------------------------------------------------ */

/*
 * Check that a memory region does not have both WRITE and EXECUTE
 * permissions simultaneously.  Returns 0 if the region is safe,
 * -1 if it violates W^X policy.
 */
#define SEC_PROT_READ  0x01
#define SEC_PROT_WRITE 0x02
#define SEC_PROT_EXEC  0x04

int security_check_wx(int prot);

#endif /* ASD_SECURITY_H */
