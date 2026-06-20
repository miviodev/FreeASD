#ifndef BIN_COMMON_H
#define BIN_COMMON_H

#include <asd/syscall.h>
#include <asd/stdio.h>
#include <asd/types.h>
#include <stddef.h>
#include <stdint.h>

extern size_t strlen(const char *);
extern int    strcmp(const char *, const char *);
extern int    strncmp(const char *, const char *, size_t);
extern char  *strrchr(const char *, int);
extern char  *strchr(const char *, int);
extern char  *strncpy(char *, const char *, size_t);
extern void  *memset(void *, int, size_t);

#define PATH_MAX_SH 256

#endif
