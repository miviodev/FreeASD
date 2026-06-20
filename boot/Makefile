# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026, ASD Project Contributors
#
# Build asdboot.efi — UEFI x86-64 application
#
# Requirements:
#   clang with Windows/PE cross target
#   llvm-objcopy for PE output

CC      = clang
CFLAGS  = --target=x86_64-unknown-windows \
          -ffreestanding \
          -fno-stack-protector \
          -mno-stack-arg-probe \
          -fno-builtin \
          -mno-red-zone \
          -nostdlib \
          -nostdinc \
          -Wall \
          -Wextra \
          -Os \
          -I. \
          -Iinclude/compat

# stdint.h shim: provide u8/u16/u32/u64 via compiler builtins
# In a real freestanding env, stdint.h is provided by the compiler (clang
# ships a freestanding stdint.h); if not available, replace with:
#   CFLAGS += -Iinclude/compat

LDFLAGS = -target x86_64-unknown-windows \
          -nostdlib \
          -Wl,-subsystem:efi_application \
          -Wl,-entry:efi_main \
          -fuse-ld=lld

TARGET = asdboot.efi

.PHONY: all clean

all: $(TARGET)

$(TARGET): asdboot.c asdboot.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ asdboot.c

clean:
	rm -f $(TARGET)
