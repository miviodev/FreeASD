/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 *
 * Serial I/O, logging, readline, and input routing for init.
 */

#include "init_internal.h"
#include "../kernel/drv/ps2kbd.h"

/* ------------------------------------------------------------------ */
/* Output                                                               */
/* ------------------------------------------------------------------ */

void serial_port_putc(char c) {
    for (uint32_t i = 0; i < 1000000; i++) {
        if ((io_in8(COM1_PORT + 5) & 0x20) != 0) break;
        __asm__ volatile("pause");
    }
    io_out8(COM1_PORT, (uint8_t)c);
}

void serial_port_puts(const char *s) {
    if (!s) return;
    while (*s) serial_port_putc(*s++);
}

void serial_putc(char c) {
    serial_port_putc(c);
    fb_console_putc(c);
}

void serial_puts(const char *s) {
    if (!s) return;
    while (*s) serial_putc(*s++);
}

void put_u64(uint64_t v) {
    char buf[32];
    int i = 31;
    buf[i] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    while (v) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    serial_puts(&buf[i]);
}

void put_repeat(char c, int n) {
    while (n-- > 0) serial_putc(c);
}

static size_t u64_len(uint64_t v) {
    if (v == 0) return 1;
    size_t n = 0;
    while (v) { n++; v /= 10; }
    return n;
}

void box_right_pad(size_t used, size_t inner_w) {
    while (used < inner_w) { serial_putc(' '); used++; }
    serial_puts("|\n");
}

/* ------------------------------------------------------------------ */
/* Logging                                                              */
/* ------------------------------------------------------------------ */

void log_msg(const char *msg) {
    serial_port_puts("[asdinit] ");
    serial_port_puts(msg);
    serial_port_puts("\n");
}

void log_svc(const char *svc, const char *msg) {
    serial_port_puts("[asdinit] ");
    serial_port_puts(svc);
    serial_port_puts(": ");
    serial_port_puts(msg);
    serial_port_puts("\n");
}

void boot_log(const char *status, const char *msg) {
    serial_port_puts("[");
    serial_port_puts(status);
    serial_port_puts("] ");
    serial_port_puts(msg);
    serial_port_puts("\n");
}

void print_shell_banner(void) {
    serial_puts("\nASD/amd64 (asdtty0)\n");
    serial_puts("login: root (auto)\n\n");
}

/* ------------------------------------------------------------------ */
/* Input                                                                */
/* ------------------------------------------------------------------ */

static int  g_input_unget_valid;
static char g_input_unget_ch;

void input_unget(char c) {
    g_input_unget_valid = 1;
    g_input_unget_ch = c;
}

int serial_getc_nonblock(char *out) {
    uint8_t lsr = io_in8(COM1_PORT + 5);
    if ((lsr & 0x01) == 0)
        return 0;
    /* Discard break/framing/parity garbage instead of feeding the TUI. */
    if (lsr & 0x1E) {
        (void)io_in8(COM1_PORT);
        return 0;
    }
    uint8_t b = io_in8(COM1_PORT);
    if (b == 0)
        return 0;
    *out = (char)b;
    return 1;
}

int kbd_getc_nonblock(char *out) {
    return ps2kbd_getc(out);
}

/* Keyboard before serial so installer fields work when COM1 has noise. */
int input_getc_nonblock(char *out) {
    if (g_input_unget_valid) {
        g_input_unget_valid = 0;
        *out = g_input_unget_ch;
        return 1;
    }
    if (kbd_getc_nonblock(out))
        return 1;
    return serial_getc_nonblock(out);
}

void flush_input(void) {
    char dummy;
    g_input_unget_valid = 0;
    /* Never spin forever — some UARTs always report data ready (hangs
     * the installer after Enter on the hostname screen). */
    for (int n = 0; n < 4096; n++) {
        int got = 0;
        if (kbd_getc_nonblock(&dummy))    got = 1;
        if (serial_getc_nonblock(&dummy)) got = 1;
        if (!got) break;
    }
}

static void input_skip_optional_lf_after_cr(void) {
    char next;
    if (input_getc_nonblock(&next) && next != '\n')
        input_unget(next);
}

static int input_wait_key(void) {
    char ch;
    for (;;) {
        while (!input_getc_nonblock(&ch)) {
            fb_console_tick();
            __asm__ volatile("pause");
        }
        return (unsigned char)ch;
    }
}

char read_char(void) {
    for (;;) {
        char ch = (char)input_wait_key();
        if (ch >= 0x20 && ch <= 0x7e) return ch;
    }
}

int read_menu_key(void) {
    for (;;) {
        char ch = (char)input_wait_key();
        if ((uint8_t)ch == 0x80) return 1; /* up   */
        if ((uint8_t)ch == 0x81) return 2; /* down */
        if (ch == '\r' || ch == '\n') return 3; /* enter */
        if (ch == 'q' || ch == 'Q' || ch == 0x1B) return 4; /* quit/esc */
    }
}

/* ------------------------------------------------------------------ */
/* Readline with history                                                */
/* ------------------------------------------------------------------ */

#define HIST_CAP 32

static char     g_hist[HIST_CAP][256];
static uint32_t g_hist_count;
static uint32_t g_hist_head;

static void hist_add(const char *line) {
    if (!line || !line[0]) return;
    uint32_t idx = (g_hist_head + g_hist_count) % HIST_CAP;
    if (g_hist_count == HIST_CAP) {
        g_hist_head = (g_hist_head + 1) % HIST_CAP;
        idx = (g_hist_head + g_hist_count - 1) % HIST_CAP;
    } else {
        g_hist_count++;
    }
    strncpy(g_hist[idx], line, sizeof(g_hist[idx]));
    g_hist[idx][sizeof(g_hist[idx]) - 1] = '\0';
}

static const char *hist_get(uint32_t rel) {
    if (rel >= g_hist_count) return NULL;
    return g_hist[(g_hist_head + rel) % HIST_CAP];
}

static void line_erase(uint32_t n) {
    while (n--) serial_puts("\b \b");
}

static void line_replace(char *buf, uint32_t *len, uint32_t cap,
                         const char *src) {
    line_erase(*len);
    uint32_t i = 0;
    while (src && src[i] && i < cap - 1) {
        buf[i] = src[i];
        serial_putc(src[i]);
        i++;
    }
    buf[i] = '\0';
    *len = i;
}

char *readline_serial(char *buf, uint32_t cap) {
    if (!buf || cap < 2) return NULL;
    uint32_t i = 0;
    int hist_cursor = -1;
    for (;;) {
        char ch = (char)input_wait_key();
        if ((uint8_t)ch == 0x80) { /* up */
            if (g_hist_count == 0) continue;
            if (hist_cursor < 0) hist_cursor = (int)g_hist_count - 1;
            else if (hist_cursor > 0) hist_cursor--;
            const char *h = hist_get((uint32_t)hist_cursor);
            if (h) line_replace(buf, &i, cap, h);
            continue;
        }
        if ((uint8_t)ch == 0x81) { /* down */
            if (g_hist_count == 0 || hist_cursor < 0) continue;
            hist_cursor++;
            if (hist_cursor >= (int)g_hist_count) {
                hist_cursor = -1;
                line_replace(buf, &i, cap, "");
            } else {
                const char *h = hist_get((uint32_t)hist_cursor);
                if (h) line_replace(buf, &i, cap, h);
            }
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r')
                input_skip_optional_lf_after_cr();
            serial_puts("\n");
            buf[i] = '\0';
            hist_add(buf);
            return buf;
        }
        if ((ch == '\b' || ch == 0x7f) && i > 0) {
            i--;
            serial_puts("\b \b");
            continue;
        }
        if (i < cap - 1 && ch >= 0x20 && ch <= 0x7e) {
            buf[i++] = ch;
            serial_putc(ch);
        }
    }
}

/* Echoes '*' per character; backspace erases. */
char *readline_serial_noecho(char *buf, uint32_t cap) {
    if (!buf || cap < 2) return NULL;
    uint32_t i = 0;
    for (;;) {
        char c = (char)input_wait_key();
        if (c == '\r' || c == '\n') {
            if (c == '\r')
                input_skip_optional_lf_after_cr();
            serial_putc('\n');
            break;
        }
        if ((c == '\b' || c == 127) && i > 0) {
            i--;
            serial_putc('\b'); serial_putc(' '); serial_putc('\b');
            continue;
        }
        if (c < 0x20) continue;
        if (i < cap - 1) { buf[i++] = c; serial_putc('*'); }
    }
    buf[i] = '\0';
    return buf;
}
