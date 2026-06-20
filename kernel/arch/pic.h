/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 */

#ifndef ASD_PIC_H
#define ASD_PIC_H

#include <stdint.h>

/* Remap 8259 PIC: master → 0x20-0x27, slave → 0x28-0x2F */
void pic_init(void);

/* Send End-Of-Interrupt for IRQ n (0-15) */
void pic_eoi(int irq);

/* Mask / unmask individual IRQ lines */
void pic_mask(int irq);
void pic_unmask(int irq);

#endif /* ASD_PIC_H */
