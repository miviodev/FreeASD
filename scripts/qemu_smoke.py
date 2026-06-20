#!/usr/bin/env python3
"""
Robust QEMU serial smoke test for OpenASD.

The script avoids fixed sleeps.  It continuously drains QEMU stdout, keeps a
rolling buffer, waits for configurable prompt regexes, and writes the complete
serial transcript to a log file.  This prevents the common failure mode where a
short-lived prompt/output chunk is produced between two timed reads and is
therefore "swallowed" by the test harness.

Example:
    python3 scripts/qemu_smoke.py \
        --kernel kernel/build/asdkernel.bin \
        --qemu qemu-system-x86_64 \
        --expect 'root@.*>' \
        --cmd 'ls' --cmd 'fastfetch' --cmd 'asded --help'

For UEFI/live-image boot, pass --qemu-arg repeatedly, for example:
    python3 scripts/qemu_smoke.py \
        --qemu-arg=-machine --qemu-arg=q35,accel=tcg \
        --qemu-arg=-drive --qemu-arg=if=pflash,format=raw,readonly=on,file=/path/OVMF_CODE.fd \
        --qemu-arg=-drive --qemu-arg=file=build/run/live.img,if=none,id=live0,format=raw \
        --qemu-arg=-device --qemu-arg=virtio-blk-pci,drive=live0,bootindex=1
"""

from __future__ import annotations

import argparse
import os
import re
import selectors
import signal
import subprocess
import sys
import time
from collections import deque
from pathlib import Path


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Robust OpenASD QEMU smoke tester")
    p.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    p.add_argument("--kernel", help="Direct -kernel boot target; omitted when --qemu-arg provides full boot config")
    p.add_argument("--qemu-arg", action="append", default=[], help="Additional raw QEMU argument, repeatable")
    p.add_argument("--expect", default=r"(root|[^\s@]+)@[^\s>]+[^\n]*>", help="Prompt regex")
    p.add_argument("--login", default="", help="Optional login name to send if Login/User prompt is seen")
    p.add_argument("--password", default="", help="Optional password to send if Password prompt is seen")
    p.add_argument("--cmd", action="append", default=["ls", "fastfetch", "asded"], help="Command to send after prompt")
    p.add_argument("--timeout", type=float, default=45.0, help="Per-wait timeout in seconds")
    p.add_argument("--log", default="build/qemu-smoke.log", help="Transcript log path")
    p.add_argument("--tail-bytes", type=int, default=65536, help="Rolling buffer size used for regex matches")
    return p.parse_args()


class Harness:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.log_path = Path(args.log)
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.log = self.log_path.open("wb")
        self.tail = deque(maxlen=args.tail_bytes)
        self.proc: subprocess.Popen[bytes] | None = None
        self.selector = selectors.DefaultSelector()

    def start(self) -> None:
        cmd = [self.args.qemu]
        if self.args.kernel:
            cmd += ["-kernel", self.args.kernel]
        if not any(a == "-serial" for a in self.args.qemu_arg):
            cmd += ["-serial", "stdio"]
        if not any(a == "-display" for a in self.args.qemu_arg):
            cmd += ["-display", "none"]
        if not any(a == "-no-reboot" for a in self.args.qemu_arg):
            cmd += ["-no-reboot"]
        cmd += list(self.args.qemu_arg)
        self.log.write(("$ " + " ".join(cmd) + "\n").encode())
        self.log.flush()
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        assert self.proc.stdout is not None
        self.selector.register(self.proc.stdout, selectors.EVENT_READ)

    def close(self) -> None:
        try:
            self.log.flush()
            self.log.close()
        finally:
            if self.proc and self.proc.poll() is None:
                self.proc.send_signal(signal.SIGTERM)
                try:
                    self.proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.proc.kill()

    def _record(self, data: bytes) -> None:
        self.log.write(data)
        self.log.flush()
        for b in data:
            self.tail.append(b)
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()

    def drain_until(self, pattern: str, timeout: float | None = None) -> re.Match[str]:
        if timeout is None:
            timeout = self.args.timeout
        deadline = time.monotonic() + timeout
        rx = re.compile(pattern, re.MULTILINE | re.DOTALL)
        while time.monotonic() < deadline:
            text = bytes(self.tail).decode("utf-8", errors="replace")
            m = rx.search(text)
            if m:
                return m
            if self.proc and self.proc.poll() is not None:
                raise RuntimeError(f"QEMU exited with status {self.proc.returncode}; see {self.log_path}")
            remaining = max(0.0, deadline - time.monotonic())
            events = self.selector.select(min(0.25, remaining))
            for key, _ in events:
                chunk = key.fileobj.read(4096)
                if chunk:
                    self._record(chunk)
        raise TimeoutError(f"Timed out waiting for {pattern!r}; transcript saved at {self.log_path}")

    def send_line(self, line: str) -> None:
        if not self.proc or not self.proc.stdin:
            raise RuntimeError("QEMU process is not running")
        data = (line + "\n").encode()
        self.log.write(b"\n>>> " + data)
        self.log.flush()
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def handle_optional_login(self) -> None:
        login_or_shell = r"(login:|Login:|User:|Password:|" + self.args.expect + r")"
        m = self.drain_until(login_or_shell)
        seen = m.group(0)
        if re.search(r"login:|Login:|User:", seen, re.I):
            self.send_line(self.args.login)
            m = self.drain_until(r"(Password:|" + self.args.expect + r")")
            if "Password" in m.group(0):
                self.send_line(self.args.password)
                self.drain_until(self.args.expect)
        elif "Password" in seen:
            self.send_line(self.args.password)
            self.drain_until(self.args.expect)


def main() -> int:
    args = parse_args()
    h = Harness(args)
    try:
        h.start()
        h.handle_optional_login()
        for cmd in args.cmd:
            h.send_line(cmd)
            h.drain_until(args.expect)
        print(f"\nSMOKE_OK transcript={h.log_path}")
        return 0
    finally:
        h.close()


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"\nSMOKE_FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
