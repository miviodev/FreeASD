/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 */

#ifndef ASD_PIT_H
#define ASD_PIT_H

#include <stdint.h>

/* Configure PIT channel 0 for periodic IRQ0 at ~100 Hz */
void pit_init(void);

/* Called from IRQ0 handler in isr_dispatch */
void pit_isr(void);

/* Monotonic tick counter (increments 100× per second) */
uint64_t pit_ticks(void);

/* Busy-wait for approximately ms milliseconds */
void pit_sleep_ms(uint32_t ms);

#endif /* ASD_PIT_H */
