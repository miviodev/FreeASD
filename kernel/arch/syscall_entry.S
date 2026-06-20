/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * SYSCALL entry point for ASD kernel.
 *
 * On entry (set by SYSCALL instruction):
 *   rcx  = user RIP (return address)
 *   r11  = user RFLAGS
 *   rax  = syscall number
 *   rdi  = arg0, rsi = arg1, rdx = arg2, r10 = arg3, r8 = arg4
 *   rsp  = user RSP (unchanged — we switch to kernel stack)
 *
 * We use SWAPGS + per-CPU struct (gs:0 = user_rsp scratch, gs:8 = kernel rsp).
 * See syscall.c: syscall_init() writes &g_percpu into KernelGSBase MSR.
 *
 * Stack layout after setup (RSP → low addr):
 *   r8, r10, rdx, rsi, rdi   — saved user arg registers
 *   user_rsp                  — saved user RSP
 *   user_rflags (r11)         — saved user RFLAGS
 *   user_rip (rcx)            — saved user RIP
 */

.code64
.text
.global syscall_entry
syscall_entry:
    swapgs
    movq  %rsp, %gs:0          /* save user RSP in per-cpu scratch */
    movq  %gs:8, %rsp          /* switch to kernel RSP */

    /* Push user state for SYSRET restoration */
    pushq %rcx                  /* user RIP */
    pushq %r11                  /* user RFLAGS */
    movq  %gs:0, %r11
    pushq %r11                  /* user RSP */

    /* Save syscall arg registers (also preserves caller state) */
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r10
    pushq %r8

    /* Shuffle into C calling convention:
     *   syscall_dispatch(nr, a0, a1, a2, a3, a4)
     *   → rdi=nr, rsi=a0, rdx=a1, rcx=a2, r8=a3, r9=a4
     * Order matters: each src is read before it's overwritten. */
    movq  %r8,  %r9             /* a4: r8  → r9  */
    movq  %r10, %r8             /* a3: r10 → r8  */
    movq  %rdx, %rcx            /* a2: rdx → rcx */
    movq  %rsi, %rdx            /* a1: rsi → rdx */
    movq  %rdi, %rsi            /* a0: rdi → rsi */
    movq  %rax, %rdi            /* nr: rax → rdi */

    call  syscall_dispatch      /* return value in rax */

    /* Check for pending signals (SIGKILL/SIGTERM → may not return) */
    pushq %rax                  /* save syscall return value */
    call  syscall_check_signals
    popq  %rax

    /* Restore arg registers (userland doesn't need them, but be clean) */
    popq  %r8
    popq  %r10
    popq  %rdx
    popq  %rsi
    popq  %rdi

    /* Restore user state for SYSRET.
     * Stack: user_rsp, user_rflags, user_rip  (RSP → user_rsp now)
     * Read rflags and rip from stack BEFORE switching RSP. */
    movq   8(%rsp), %r11        /* r11 = user RFLAGS */
    movq  16(%rsp), %rcx        /* rcx = user RIP */
    movq   (%rsp),  %rsp        /* switch back to user RSP */

    swapgs
    sysretq                     /* restores CS/SS from STAR, rcx→RIP, r11→RFLAGS */
