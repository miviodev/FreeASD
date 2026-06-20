/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * 8259A PIC driver — remaps IRQs to vectors 0x20-0x2F.
 */

#include "pic.h"
#include <stdint.h>

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define PIC_EOI    0x20

#define ICW1_ICW4  0x01
#define ICW1_INIT  0x10
#define ICW4_8086  0x01

static inline void io_out8(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));
}
static inline uint8_t io_in8(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p)); return v;
}
static inline void io_wait(void) {
    /* Tiny delay via a no-op out to port 0x80 (POST port) */
    __asm__ volatile("outb %al, $0x80");
}

void pic_init(void) {
    /* Save masks */
    uint8_t m1 = io_in8(PIC1_DATA);
    uint8_t m2 = io_in8(PIC2_DATA);

    /* ICW1 — start initialisation */
    io_out8(PIC1_CMD,  ICW1_INIT | ICW1_ICW4);  io_wait();
    io_out8(PIC2_CMD,  ICW1_INIT | ICW1_ICW4);  io_wait();

    /* ICW2 — vector offsets */
    io_out8(PIC1_DATA, 0x20); io_wait();   /* master: IRQ0-7 → 0x20-0x27 */
    io_out8(PIC2_DATA, 0x28); io_wait();   /* slave:  IRQ8-15 → 0x28-0x2F */

    /* ICW3 — cascade */
    io_out8(PIC1_DATA, 0x04); io_wait();   /* master: slave on IRQ2 */
    io_out8(PIC2_DATA, 0x02); io_wait();   /* slave: cascade identity 2 */

    /* ICW4 — 8086 mode */
    io_out8(PIC1_DATA, ICW4_8086); io_wait();
    io_out8(PIC2_DATA, ICW4_8086); io_wait();

    /* Restore masks */
    io_out8(PIC1_DATA, m1);
    io_out8(PIC2_DATA, m2);
}

void pic_eoi(int irq) {
    if (irq >= 8)
        io_out8(PIC2_CMD, PIC_EOI);
    io_out8(PIC1_CMD, PIC_EOI);
}

void pic_mask(int irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (uint8_t)(irq < 8 ? irq : irq - 8);
    io_out8(port, io_in8(port) | (uint8_t)(1 << bit));
}

void pic_unmask(int irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t  bit  = (uint8_t)(irq < 8 ? irq : irq - 8);
    io_out8(port, io_in8(port) & (uint8_t)~(1 << bit));
}
