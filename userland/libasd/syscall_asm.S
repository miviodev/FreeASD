/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * __syscall(nr, a0, a1, a2, a3, a4) — x86-64 SYSCALL instruction wrapper.
 *
 * C prototype:  long __syscall(long nr, long a0, long a1, long a2, long a3, long a4);
 *
 * Kernel expects: rdi=nr, rsi=a0, rdx=a1, rcx=a2, r8=a3, r9=a4
 * But for arg3 (4th arg from C call) the kernel reads r10 not rcx,
 * because SYSCALL overwrites rcx with user RIP.
 * So we move a3 (arrived in rcx) to r10 before the syscall.
 */

.code64
.text
.global ___syscall
___syscall:
    /* C args arrive as:
     *   rdi=nr, rsi=a0, rdx=a1, rcx=a2, r8=a3, r9=a4
     * Kernel convention:
     *   rax=nr, rdi=a0, rsi=a1, rdx=a2, r10=a3, r8=a4
     */
    movq  %rdi, %rax      /* nr  → rax */
    movq  %rsi, %rdi      /* a0  → rdi */
    movq  %rdx, %rsi      /* a1  → rsi */
    movq  %rcx, %rdx      /* a2  → rdx */
    movq  %r8,  %r10      /* a3  → r10 */
    movq  %r9,  %r8       /* a4  → r8  */
    syscall
    ret
