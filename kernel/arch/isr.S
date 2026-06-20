/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * ISR stubs for all 256 interrupt vectors.
 *
 * Stack layout when isr_common is reached (RSP → low addr):
 *   r15, r14, r13, r12, r11, r10, r9, r8, rbp, rdi, rsi, rdx, rcx, rbx, rax
 *   vector, error_code (real or 0)
 *   rip, cs, rflags, rsp, ss   ← pushed by CPU (always in 64-bit mode)
 *
 * The C struct isr_frame_t mirrors this layout with r15 at offset 0.
 */

.code64
.text

/* ------------------------------------------------------------------ */
/* Two flavours of stub: with vs. without CPU-pushed error code        */
/* ------------------------------------------------------------------ */
.macro STUB_NOERR vec
.align 4
.global isr_stub_\vec
isr_stub_\vec:
    pushq $0          /* fake error code */
    pushq $\vec
    jmp   isr_common
.endm

.macro STUB_ERR vec
.align 4
.global isr_stub_\vec
isr_stub_\vec:
    pushq $\vec       /* error code already on stack */
    jmp   isr_common
.endm

/* Vectors 0-7: no error code */
STUB_NOERR 0; STUB_NOERR 1; STUB_NOERR 2; STUB_NOERR 3
STUB_NOERR 4; STUB_NOERR 5; STUB_NOERR 6; STUB_NOERR 7

/* Vector 8: double fault — has error code */
STUB_ERR 8

/* Vector 9: no error code */
STUB_NOERR 9

/* Vectors 10-14: have error code */
STUB_ERR 10; STUB_ERR 11; STUB_ERR 12; STUB_ERR 13; STUB_ERR 14

/* Vectors 15-16: no error code */
STUB_NOERR 15; STUB_NOERR 16

/* Vector 17: alignment check — has error code */
STUB_ERR 17

/* Vectors 18-20: no error code */
STUB_NOERR 18; STUB_NOERR 19; STUB_NOERR 20

/* Vector 21: control protection — has error code */
STUB_ERR 21

/* Vectors 22-28: no error code */
STUB_NOERR 22; STUB_NOERR 23; STUB_NOERR 24; STUB_NOERR 25
STUB_NOERR 26; STUB_NOERR 27; STUB_NOERR 28

/* Vector 29: VMM communication — has error code */
STUB_ERR 29

/* Vector 30: security exception — has error code */
STUB_ERR 30

/* Vector 31: no error code */
STUB_NOERR 31

/* Vectors 32-255: IRQs / software interrupts — no error code */
STUB_NOERR 32;  STUB_NOERR 33;  STUB_NOERR 34;  STUB_NOERR 35
STUB_NOERR 36;  STUB_NOERR 37;  STUB_NOERR 38;  STUB_NOERR 39
STUB_NOERR 40;  STUB_NOERR 41;  STUB_NOERR 42;  STUB_NOERR 43
STUB_NOERR 44;  STUB_NOERR 45;  STUB_NOERR 46;  STUB_NOERR 47
STUB_NOERR 48;  STUB_NOERR 49;  STUB_NOERR 50;  STUB_NOERR 51
STUB_NOERR 52;  STUB_NOERR 53;  STUB_NOERR 54;  STUB_NOERR 55
STUB_NOERR 56;  STUB_NOERR 57;  STUB_NOERR 58;  STUB_NOERR 59
STUB_NOERR 60;  STUB_NOERR 61;  STUB_NOERR 62;  STUB_NOERR 63
STUB_NOERR 64;  STUB_NOERR 65;  STUB_NOERR 66;  STUB_NOERR 67
STUB_NOERR 68;  STUB_NOERR 69;  STUB_NOERR 70;  STUB_NOERR 71
STUB_NOERR 72;  STUB_NOERR 73;  STUB_NOERR 74;  STUB_NOERR 75
STUB_NOERR 76;  STUB_NOERR 77;  STUB_NOERR 78;  STUB_NOERR 79
STUB_NOERR 80;  STUB_NOERR 81;  STUB_NOERR 82;  STUB_NOERR 83
STUB_NOERR 84;  STUB_NOERR 85;  STUB_NOERR 86;  STUB_NOERR 87
STUB_NOERR 88;  STUB_NOERR 89;  STUB_NOERR 90;  STUB_NOERR 91
STUB_NOERR 92;  STUB_NOERR 93;  STUB_NOERR 94;  STUB_NOERR 95
STUB_NOERR 96;  STUB_NOERR 97;  STUB_NOERR 98;  STUB_NOERR 99
STUB_NOERR 100; STUB_NOERR 101; STUB_NOERR 102; STUB_NOERR 103
STUB_NOERR 104; STUB_NOERR 105; STUB_NOERR 106; STUB_NOERR 107
STUB_NOERR 108; STUB_NOERR 109; STUB_NOERR 110; STUB_NOERR 111
STUB_NOERR 112; STUB_NOERR 113; STUB_NOERR 114; STUB_NOERR 115
STUB_NOERR 116; STUB_NOERR 117; STUB_NOERR 118; STUB_NOERR 119
STUB_NOERR 120; STUB_NOERR 121; STUB_NOERR 122; STUB_NOERR 123
STUB_NOERR 124; STUB_NOERR 125; STUB_NOERR 126; STUB_NOERR 127
STUB_NOERR 128; STUB_NOERR 129; STUB_NOERR 130; STUB_NOERR 131
STUB_NOERR 132; STUB_NOERR 133; STUB_NOERR 134; STUB_NOERR 135
STUB_NOERR 136; STUB_NOERR 137; STUB_NOERR 138; STUB_NOERR 139
STUB_NOERR 140; STUB_NOERR 141; STUB_NOERR 142; STUB_NOERR 143
STUB_NOERR 144; STUB_NOERR 145; STUB_NOERR 146; STUB_NOERR 147
STUB_NOERR 148; STUB_NOERR 149; STUB_NOERR 150; STUB_NOERR 151
STUB_NOERR 152; STUB_NOERR 153; STUB_NOERR 154; STUB_NOERR 155
STUB_NOERR 156; STUB_NOERR 157; STUB_NOERR 158; STUB_NOERR 159
STUB_NOERR 160; STUB_NOERR 161; STUB_NOERR 162; STUB_NOERR 163
STUB_NOERR 164; STUB_NOERR 165; STUB_NOERR 166; STUB_NOERR 167
STUB_NOERR 168; STUB_NOERR 169; STUB_NOERR 170; STUB_NOERR 171
STUB_NOERR 172; STUB_NOERR 173; STUB_NOERR 174; STUB_NOERR 175
STUB_NOERR 176; STUB_NOERR 177; STUB_NOERR 178; STUB_NOERR 179
STUB_NOERR 180; STUB_NOERR 181; STUB_NOERR 182; STUB_NOERR 183
STUB_NOERR 184; STUB_NOERR 185; STUB_NOERR 186; STUB_NOERR 187
STUB_NOERR 188; STUB_NOERR 189; STUB_NOERR 190; STUB_NOERR 191
STUB_NOERR 192; STUB_NOERR 193; STUB_NOERR 194; STUB_NOERR 195
STUB_NOERR 196; STUB_NOERR 197; STUB_NOERR 198; STUB_NOERR 199
STUB_NOERR 200; STUB_NOERR 201; STUB_NOERR 202; STUB_NOERR 203
STUB_NOERR 204; STUB_NOERR 205; STUB_NOERR 206; STUB_NOERR 207
STUB_NOERR 208; STUB_NOERR 209; STUB_NOERR 210; STUB_NOERR 211
STUB_NOERR 212; STUB_NOERR 213; STUB_NOERR 214; STUB_NOERR 215
STUB_NOERR 216; STUB_NOERR 217; STUB_NOERR 218; STUB_NOERR 219
STUB_NOERR 220; STUB_NOERR 221; STUB_NOERR 222; STUB_NOERR 223
STUB_NOERR 224; STUB_NOERR 225; STUB_NOERR 226; STUB_NOERR 227
STUB_NOERR 228; STUB_NOERR 229; STUB_NOERR 230; STUB_NOERR 231
STUB_NOERR 232; STUB_NOERR 233; STUB_NOERR 234; STUB_NOERR 235
STUB_NOERR 236; STUB_NOERR 237; STUB_NOERR 238; STUB_NOERR 239
STUB_NOERR 240; STUB_NOERR 241; STUB_NOERR 242; STUB_NOERR 243
STUB_NOERR 244; STUB_NOERR 245; STUB_NOERR 246; STUB_NOERR 247
STUB_NOERR 248; STUB_NOERR 249; STUB_NOERR 250; STUB_NOERR 251
STUB_NOERR 252; STUB_NOERR 253; STUB_NOERR 254; STUB_NOERR 255

/* ------------------------------------------------------------------ */
/* Common path                                                          */
/* ------------------------------------------------------------------ */
.align 16
isr_common:
    /* Push GPRs — must match isr_frame_t (r15 at lowest addr = RSP) */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* RSP now = &frame->r15 = isr_frame_t pointer.
     * Save it in r15 (callee-saved; we'll use it to restore RSP after call).
     * The saved-on-stack r15 is still correct for the pop below. */
    movq  %rsp, %r15

    /* Align RSP to 16 bytes for C ABI, then call */
    andq  $-16, %rsp
    movq  %r15, %rdi          /* arg0 = isr_frame_t * */
    call  isr_dispatch

    /* Restore unaligned RSP */
    movq  %r15, %rsp

    /* Pop GPRs (r15 restores caller's r15 from the saved slot) */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp    /* discard vector + error_code */
    iretq

/* ------------------------------------------------------------------ */
/* Pointer table — isr_stubs[256]                                      */
/* ------------------------------------------------------------------ */
.section .rodata
.align 8
.global isr_stubs
isr_stubs:
.quad isr_stub_0,   isr_stub_1,   isr_stub_2,   isr_stub_3
.quad isr_stub_4,   isr_stub_5,   isr_stub_6,   isr_stub_7
.quad isr_stub_8,   isr_stub_9,   isr_stub_10,  isr_stub_11
.quad isr_stub_12,  isr_stub_13,  isr_stub_14,  isr_stub_15
.quad isr_stub_16,  isr_stub_17,  isr_stub_18,  isr_stub_19
.quad isr_stub_20,  isr_stub_21,  isr_stub_22,  isr_stub_23
.quad isr_stub_24,  isr_stub_25,  isr_stub_26,  isr_stub_27
.quad isr_stub_28,  isr_stub_29,  isr_stub_30,  isr_stub_31
.quad isr_stub_32,  isr_stub_33,  isr_stub_34,  isr_stub_35
.quad isr_stub_36,  isr_stub_37,  isr_stub_38,  isr_stub_39
.quad isr_stub_40,  isr_stub_41,  isr_stub_42,  isr_stub_43
.quad isr_stub_44,  isr_stub_45,  isr_stub_46,  isr_stub_47
.quad isr_stub_48,  isr_stub_49,  isr_stub_50,  isr_stub_51
.quad isr_stub_52,  isr_stub_53,  isr_stub_54,  isr_stub_55
.quad isr_stub_56,  isr_stub_57,  isr_stub_58,  isr_stub_59
.quad isr_stub_60,  isr_stub_61,  isr_stub_62,  isr_stub_63
.quad isr_stub_64,  isr_stub_65,  isr_stub_66,  isr_stub_67
.quad isr_stub_68,  isr_stub_69,  isr_stub_70,  isr_stub_71
.quad isr_stub_72,  isr_stub_73,  isr_stub_74,  isr_stub_75
.quad isr_stub_76,  isr_stub_77,  isr_stub_78,  isr_stub_79
.quad isr_stub_80,  isr_stub_81,  isr_stub_82,  isr_stub_83
.quad isr_stub_84,  isr_stub_85,  isr_stub_86,  isr_stub_87
.quad isr_stub_88,  isr_stub_89,  isr_stub_90,  isr_stub_91
.quad isr_stub_92,  isr_stub_93,  isr_stub_94,  isr_stub_95
.quad isr_stub_96,  isr_stub_97,  isr_stub_98,  isr_stub_99
.quad isr_stub_100, isr_stub_101, isr_stub_102, isr_stub_103
.quad isr_stub_104, isr_stub_105, isr_stub_106, isr_stub_107
.quad isr_stub_108, isr_stub_109, isr_stub_110, isr_stub_111
.quad isr_stub_112, isr_stub_113, isr_stub_114, isr_stub_115
.quad isr_stub_116, isr_stub_117, isr_stub_118, isr_stub_119
.quad isr_stub_120, isr_stub_121, isr_stub_122, isr_stub_123
.quad isr_stub_124, isr_stub_125, isr_stub_126, isr_stub_127
.quad isr_stub_128, isr_stub_129, isr_stub_130, isr_stub_131
.quad isr_stub_132, isr_stub_133, isr_stub_134, isr_stub_135
.quad isr_stub_136, isr_stub_137, isr_stub_138, isr_stub_139
.quad isr_stub_140, isr_stub_141, isr_stub_142, isr_stub_143
.quad isr_stub_144, isr_stub_145, isr_stub_146, isr_stub_147
.quad isr_stub_148, isr_stub_149, isr_stub_150, isr_stub_151
.quad isr_stub_152, isr_stub_153, isr_stub_154, isr_stub_155
.quad isr_stub_156, isr_stub_157, isr_stub_158, isr_stub_159
.quad isr_stub_160, isr_stub_161, isr_stub_162, isr_stub_163
.quad isr_stub_164, isr_stub_165, isr_stub_166, isr_stub_167
.quad isr_stub_168, isr_stub_169, isr_stub_170, isr_stub_171
.quad isr_stub_172, isr_stub_173, isr_stub_174, isr_stub_175
.quad isr_stub_176, isr_stub_177, isr_stub_178, isr_stub_179
.quad isr_stub_180, isr_stub_181, isr_stub_182, isr_stub_183
.quad isr_stub_184, isr_stub_185, isr_stub_186, isr_stub_187
.quad isr_stub_188, isr_stub_189, isr_stub_190, isr_stub_191
.quad isr_stub_192, isr_stub_193, isr_stub_194, isr_stub_195
.quad isr_stub_196, isr_stub_197, isr_stub_198, isr_stub_199
.quad isr_stub_200, isr_stub_201, isr_stub_202, isr_stub_203
.quad isr_stub_204, isr_stub_205, isr_stub_206, isr_stub_207
.quad isr_stub_208, isr_stub_209, isr_stub_210, isr_stub_211
.quad isr_stub_212, isr_stub_213, isr_stub_214, isr_stub_215
.quad isr_stub_216, isr_stub_217, isr_stub_218, isr_stub_219
.quad isr_stub_220, isr_stub_221, isr_stub_222, isr_stub_223
.quad isr_stub_224, isr_stub_225, isr_stub_226, isr_stub_227
.quad isr_stub_228, isr_stub_229, isr_stub_230, isr_stub_231
.quad isr_stub_232, isr_stub_233, isr_stub_234, isr_stub_235
.quad isr_stub_236, isr_stub_237, isr_stub_238, isr_stub_239
.quad isr_stub_240, isr_stub_241, isr_stub_242, isr_stub_243
.quad isr_stub_244, isr_stub_245, isr_stub_246, isr_stub_247
.quad isr_stub_248, isr_stub_249, isr_stub_250, isr_stub_251
.quad isr_stub_252, isr_stub_253, isr_stub_254, isr_stub_255
