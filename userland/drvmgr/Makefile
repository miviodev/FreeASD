.PHONY: all clean

CC     = clang
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L

all: dctl

dctl: dctl.c
	$(CC) $(CFLAGS) -o $@ dctl.c

clean:
	rm -f dctl
