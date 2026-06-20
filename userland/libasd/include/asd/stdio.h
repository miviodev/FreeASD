/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 */

#ifndef LIBASD_STDIO_H
#define LIBASD_STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* fd 1 = stdout (serial+fb), fd 0 = stdin (keyboard) */
#define STDIN_FD  0
#define STDOUT_FD 1
#define STDERR_FD 2

void  puts(const char *s);
void  putc(char c);
int   printf(const char *fmt, ...);
int   vprintf(const char *fmt, va_list ap);
int   snprintf(char *buf, size_t n, const char *fmt, ...);
int   vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

/* Read a line from stdin into buf (up to cap-1 chars), null-terminated.
 * Returns number of chars read (excluding newline). */
int   readline(char *buf, int cap);

#endif /* LIBASD_STDIO_H */
