# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, ASD Project Contributors
#
# Build asdinit (PID 1) — initially linked into kernel image

CC      = clang
CFLAGS  = --target=x86_64-unknown-windows \
          -ffreestanding \
          -fno-stack-protector \
          -fno-builtin \
          -mno-red-zone \
          -nostdlib \
          -nostdinc \
          -Wall \
          -Wextra \
          -Wno-unused-function \
          -Os \
          -I.

SRCS = asdinit.c installer.c installer_tui.c io.c shell.c svcmgr.c
OBJS = $(SRCS:.c=.o)
TARGET = asdinit.o

# Merge all objects into one asdinit.o for the kernel to link
$(TARGET): $(OBJS)
	$(LD) -m elf_x86_64 -r -o $@ $(OBJS)

.PHONY: all clean

all: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
