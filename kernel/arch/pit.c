/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * 8253/8254 PIT — channel 0, periodic, ~100 Hz.
 */

#include "pit.h"
#include "../sched/sched.h"
#include <stdint.h>

#define PIT_CH0    0x40
#define PIT_CMD    0x43

/* PIT base frequency */
#define PIT_HZ     1193182UL

/* Target: 100 Hz → divisor = 11932 */
#define PIT_DIVISOR  11932U

static volatile uint64_t g_ticks = 0;

static inline void io_out8(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}

void pit_init(void) {
    /* Mode 2 (rate generator), binary, channel 0, lo/hi */
    io_out8(PIT_CMD, 0x34);
    io_out8(PIT_CH0, (uint8_t)(PIT_DIVISOR & 0xFF));
    io_out8(PIT_CH0, (uint8_t)(PIT_DIVISOR >> 8));
}

void pit_isr(void) {
    uint64_t t = g_ticks + 1;
    g_ticks = t;

    /* Notify scheduler — convert tick to nanoseconds */
    uint64_t ns = t * (1000000000ULL / 100ULL);
    sched_tick(ns);
}

uint64_t pit_ticks(void) {
    return g_ticks;
}

void pit_sleep_ms(uint32_t ms) {
    uint64_t target = g_ticks + (uint64_t)ms / 10 + 1;
    while (g_ticks < target)
        __asm__ volatile("pause");
}
