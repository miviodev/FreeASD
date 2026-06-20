#!/usr/bin/env python3
"""
Autotest: boot OpenASD live shell, run mifetch twice, verify both complete.

Tests the bug where the second invocation of mifetch caused the VM to hang
with no output. Both runs must produce the "OpenASD mifetch" banner and the
system info fields within the timeout.

Usage (from repo root, after `make prepare-run`):
    python3 scripts/test_mifetch_twice.py
"""

import os
import re
import subprocess
import sys
import time
import signal
from pathlib import Path

CWD      = Path(__file__).parent.parent
QEMU     = os.environ.get("QEMU", "qemu-system-x86_64")
OVMF_CODE = os.environ.get(
    "OVMF_CODE",
    r"C:\msys64\ucrt64\share\qemu\edk2-x86_64-code.fd",
)
OVMF_VARS = "build/run/OVMF_VARS.fd"
LIVE_IMG  = "build/run/live.img"
DISK_IMG  = "build/disk/asd-target.img"

BOOT_TIMEOUT   = 40.0   # seconds to wait for boot menu
SELECT_TIMEOUT = 10.0   # seconds after selecting shell
RUN_TIMEOUT    = 15.0   # seconds per mifetch run


def read_nonblock(proc, buf: list, timeout: float, pattern: str) -> re.Match | None:
    """Drain proc.stdout until pattern matches or timeout expires."""
    rx = re.compile(pattern, re.DOTALL)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            chunk = proc.stdout.read(4096)
            if chunk:
                text = chunk.decode("utf-8", errors="replace")
                sys.stdout.write(text)
                sys.stdout.flush()
                buf.append(text)
        except BlockingIOError:
            pass
        full = "".join(buf)
        m = rx.search(full)
        if m:
            return m
        if proc.poll() is not None:
            raise RuntimeError(f"QEMU exited early (code {proc.returncode})")
        time.sleep(0.05)
    return None


def send(proc, line: str) -> None:
    print(f"\n>>> {line!r}")
    proc.stdin.write((line + "\n").encode())
    proc.stdin.flush()


def main() -> int:
    cmd = [
        QEMU,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64",
        "-m", "1024",
        "-display", "none",
        "-no-reboot",
        "-serial", "stdio",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
        "-drive", f"if=pflash,format=raw,file={OVMF_VARS}",
        "-drive", f"file={LIVE_IMG},if=none,id=live0,format=raw",
        "-device", "virtio-blk-pci,drive=live0,bootindex=1",
        "-drive", f"file={DISK_IMG},if=none,id=inst0,format=raw",
        "-device", "virtio-blk-pci,drive=inst0,bootindex=2",
    ]

    print("Launching QEMU:", " ".join(cmd))
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=str(CWD),
        bufsize=0,
    )
    os.set_blocking(proc.stdout.fileno(), False)
    buf: list[str] = []

    try:
        # --- Phase 1: wait for boot menu ---
        print("\n[1] Waiting for boot menu...")
        m = read_nonblock(proc, buf, BOOT_TIMEOUT,
                          r"Press I to install or S for live shell:")
        if not m:
            raise TimeoutError("Boot menu did not appear within timeout")

        # --- Phase 2: select live shell ---
        print("\n[2] Selecting live shell...")
        send(proc, "S")
        m = read_nonblock(proc, buf, SELECT_TIMEOUT, r"#\s*$")
        if not m:
            raise TimeoutError("Shell prompt did not appear after selecting 'S'")

        # --- Phase 3: first mifetch run ---
        print("\n[3] Running mifetch (first time)...")
        buf.clear()
        send(proc, "mifetch")
        m = read_nonblock(proc, buf, RUN_TIMEOUT, r"OpenASD mifetch")
        if not m:
            raise AssertionError("First mifetch produced no output (hung or missing binary)")
        # Wait for prompt to return
        m = read_nonblock(proc, buf, RUN_TIMEOUT, r"#\s*$")
        if not m:
            raise AssertionError("Shell prompt did not return after first mifetch")

        first_output = "".join(buf)
        assert "OS:" in first_output or "Kernel:" in first_output, \
            "First mifetch output is incomplete"
        print("\n[3] First mifetch completed OK.")

        # --- Phase 4: second mifetch run (the bug: VM hangs here) ---
        print("\n[4] Running mifetch (second time)...")
        buf.clear()
        send(proc, "mifetch")
        m = read_nonblock(proc, buf, RUN_TIMEOUT, r"OpenASD mifetch")
        if not m:
            raise AssertionError(
                "Second mifetch produced no output — VM hung (the bug is NOT fixed)"
            )
        m = read_nonblock(proc, buf, RUN_TIMEOUT, r"#\s*$")
        if not m:
            raise AssertionError("Shell prompt did not return after second mifetch")

        second_output = "".join(buf)
        assert "OS:" in second_output or "Kernel:" in second_output, \
            "Second mifetch output is incomplete"
        print("\n[4] Second mifetch completed OK.")

        print("\n=== TEST PASSED: mifetch ran successfully twice ===")
        return 0

    except Exception as e:
        print(f"\n=== TEST FAILED: {e} ===", file=sys.stderr)
        return 1

    finally:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()


if __name__ == "__main__":
    sys.exit(main())
