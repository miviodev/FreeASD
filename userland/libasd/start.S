/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * _start — Mach-O entry point for all ASD userland programs.
 *
 * On entry the kernel has set up the System V AMD64 ABI initial stack
 * (see kernel/arch/macho.c: build_stack_macho):
 *
 *   [rsp+0]              argc          (uint64_t)
 *   [rsp+8]              argv[0]       (pointer to string)
 *   ...
 *   [rsp+8*(argc+1)]     NULL          (end of argv)
 *   [rsp+8*(argc+2)]     envp[0]       (pointer to string)
 *   ...
 *                        NULL          (end of envp)
 *
 * We extract argc, argv, envp and call main(argc, argv, envp).
 * After main returns we call SYS_EXIT with the return value.
 */

.code64
.text
.global _start
_start:
    /* Clear frame pointer — marks bottom of call stack for debuggers */
    xorq    %rbp, %rbp

    /* argc is at [rsp] */
    movq    (%rsp), %rdi            /* argc → rdi (1st arg to main) */

    /* argv = rsp + 8 */
    leaq    8(%rsp), %rsi           /* argv → rsi (2nd arg to main) */

    /* envp = rsp + 16 + argc*8 */
    movq    %rdi, %rdx
    shlq    $3, %rdx
    leaq    16(%rsp,%rdx), %rdx     /* envp → rdx (3rd arg to main) */

    /* Align stack to 16 bytes before the call. */
    pushq   %r12
    movq    %rsp, %r12
    andq    $-16, %rsp
    subq    $8, %rsp
    call    _main
    movq    %r12, %rsp
    popq    %r12

    /* main returned — call SYS_EXIT(return_value) */
    movq    %rax, %rdi
    movq    $1, %rax                /* SYS_EXIT = 1 */
    syscall

.Lhalt:
    hlt
    jmp     .Lhalt
