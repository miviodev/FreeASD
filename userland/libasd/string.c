/*
 * SPDX-License-Identifier: BSD-2-Clause
 * Copyright (c) 2026, ASD Project Contributors
 */

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *d, const void *s, size_t n) {
    uint8_t *dd = d; const uint8_t *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
void *memset(void *d, int v, size_t n) {
    uint8_t *dd = d;
    while (n--) *dd++ = (uint8_t)v;
    return d;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *aa = a, *bb = b;
    while (n--) { if (*aa != *bb) return *aa - *bb; aa++; bb++; }
    return 0;
}
size_t strlen(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!a[i] || a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
    }
    return 0;
}
char *strcpy(char *d, const char *s) {
    char *r = d;
    while ((*d++ = *s++) != '\0') {}
    return r;
}
char *strncpy(char *d, const char *s, size_t n) {
    size_t i = 0;
    while (i < n - 1 && s[i]) { d[i] = s[i]; i++; }
    d[i] = '\0';
    return d;
}
char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char *)s; s++; }
    return (c == 0) ? (char *)s : NULL;
}
char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) { if (*s == (char)c) last = s; s++; }
    return (char *)last;
}
int atoi(const char *s) {
    int n = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

void bzero(void *d, size_t n) {
    uint8_t *p = (uint8_t *)d;
    while (n--) *p++ = 0;
}

/* Mach-O alias: Rust/LLVM emits ___bzero (= C __bzero) for large zero-inits */
void __bzero(void *d, size_t n) { bzero(d, n); }

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}
