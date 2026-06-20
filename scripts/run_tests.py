#!/usr/bin/env python3
"""
OpenASD automated test runner.
Boots QEMU with the live image, logs in as root, runs all test programs,
collects results, and prints a summary.

Usage:
    python3 scripts/run_tests.py [--accel kvm|tcg] [--timeout 120]
"""
from __future__ import annotations
import argparse, os, re, selectors, signal, subprocess, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"

def find_ovmf():
    candidates = [
        "/usr/share/edk2/x64/OVMF_CODE.4m.fd",
        "/usr/share/OVMF/OVMF_CODE.fd",
        "/usr/share/qemu/OVMF.fd",
        "/ucrt64/share/qemu/edk2-x86_64-code.fd",
        "/usr/share/edk2-ovmf/x64/OVMF_CODE.fd",
    ]
    for p in candidates:
        if os.path.isfile(p): return p
    return None

def find_ovmf_vars():
    candidates = [
        "/usr/share/edk2/x64/OVMF_VARS.4m.fd",
        "/usr/share/OVMF/OVMF_VARS.fd",
        "/usr/share/qemu/OVMF_VARS.fd",
        "/ucrt64/share/qemu/edk2-i386-vars.fd",
        "/usr/share/edk2-ovmf/x64/OVMF_VARS.fd",
    ]
    for p in candidates:
        if os.path.isfile(p): return p
    return None

class VM:
    def __init__(self, accel="tcg", timeout=120):
        self.accel = accel
        self.timeout = timeout
        self.proc = None
        self.sel = selectors.DefaultSelector()
        self._buf = b""
        self.log = []

    def start(self):
        live = BUILD / "run" / "live.img"
        vars_src = find_ovmf_vars()
        vars_run = BUILD / "run" / "OVMF_VARS.fd"
        ovmf = find_ovmf()

        if not live.exists():
            sys.exit(f"[FAIL] live.img not found at {live} — run: make prepare-run")
        if not ovmf:
            sys.exit("[FAIL] OVMF_CODE not found — install edk2-ovmf")

        # Copy VARS if not present
        vars_run.parent.mkdir(parents=True, exist_ok=True)
        if vars_src and not vars_run.exists():
            import shutil; shutil.copy(vars_src, vars_run)

        cmd = [
            "qemu-system-x86_64",
            "-machine", f"q35,accel={self.accel}:tcg",
            "-cpu", "qemu64",
            "-m", "512",
            "-display", "none",
            "-no-reboot",
            "-serial", "stdio",
            "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf}",
        ]
        if vars_run.exists():
            cmd += ["-drive", f"if=pflash,format=raw,file={vars_run}"]
        cmd += [
            "-drive", f"file={live},if=none,id=live0,format=raw",
            "-device", "virtio-blk-pci,drive=live0,bootindex=1",
            "-netdev", "user,id=net0,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3",
            "-device", "virtio-net-pci,netdev=net0",
        ]
        print("[run]", " ".join(cmd))
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, bufsize=0)
        self.sel.register(self.proc.stdout, selectors.EVENT_READ)

    def _read(self, timeout=0.5):
        evs = self.sel.select(timeout)
        for key, _ in evs:
            data = key.fileobj.read(4096)
            if data:
                self._buf += data
                sys.stdout.buffer.write(data); sys.stdout.buffer.flush()
                self.log.append(data)

    def wait_for(self, pattern, timeout=None):
        if timeout is None: timeout = self.timeout
        rx = re.compile(pattern.encode(), re.DOTALL)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._read(0.2)
            if rx.search(self._buf): return True
            if self.proc.poll() is not None: return False
        return False

    def send(self, cmd):
        print(f"[send] {cmd!r}")
        self.proc.stdin.write((cmd + "\n").encode())
        self.proc.stdin.flush()

    def stop(self):
        if self.proc and self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
            try: self.proc.wait(timeout=5)
            except: self.proc.kill()

    def full_log(self):
        return b"".join(self.log).decode("utf-8", errors="replace")


TESTS = [
    # (name, command, success_pattern, fail_pattern)
    ("mkdir_tmp",   "mkdir /tmp",           r"(#|\$|\btmp\b|>)",      None),
    ("touch_file",  "touch /tmp/t.txt",     r"(#|\$|>)",              None),
    ("ls_tmp",      "ls /tmp",              r"t\.txt",                r"cannot open"),
    ("cat_write",   "echo hello > /dev/null; touch /tmp/w.txt",
                    r"(#|\$|>)", None),
    ("filetest",    "filetest",             r"FILETEST OK",           r"FILETEST FAIL"),
    ("nettest",     "nettest",              r"NETTEST PING OK",       r"NETTEST PING FAIL"),
    ("ping_gw",     "ping 10.0.2.2",        r"bytes from",            r"100% packet loss"),
]


def run_tests(accel="tcg", timeout=120):
    vm = VM(accel=accel, timeout=timeout)
    results = {}
    try:
        vm.start()
        print("\n[*] Waiting for boot...")
        if not vm.wait_for(r"(login:|OpenASD)", timeout=90):
            print("[FAIL] Boot timeout")
            return {"boot": False}

        # Login as root (empty password)
        log_text = vm.full_log()
        if "login:" in log_text or "Login:" in log_text:
            vm.send("root")
            if not vm.wait_for(r"(Password:|#|\$|>)", timeout=15):
                print("[FAIL] Login prompt timeout")
                return {"boot": False}
            if "Password" in vm.full_log().split("login:")[-1]:
                vm.send("")  # empty password
                if not vm.wait_for(r"(#|\$|>)", timeout=10):
                    print("[FAIL] Password timeout")
                    return {"boot": False}

        # Wait for shell prompt
        if not vm.wait_for(r"(#|\$|>)", timeout=30):
            print("[FAIL] Shell prompt timeout")
            return {"shell": False}

        print("\n[*] Running tests...\n")

        for name, cmd, ok_pat, fail_pat in TESTS:
            # Clear buffer tail and send command
            before = len(vm._buf)
            vm.send(cmd)

            # Wait for next prompt (indicates command finished)
            done = vm.wait_for(r"(#|\$|>)\s*$", timeout=timeout)
            after_text = vm._buf[before:].decode("utf-8", errors="replace")

            if not done:
                results[name] = ("TIMEOUT", "")
                continue

            if ok_pat and re.search(ok_pat, after_text):
                results[name] = ("OK", "")
            elif fail_pat and re.search(fail_pat, after_text):
                results[name] = ("FAIL", after_text.strip()[-200:])
            elif ok_pat:
                results[name] = ("FAIL", f"pattern {ok_pat!r} not found")
            else:
                results[name] = ("OK", "")

    finally:
        vm.stop()

    return results


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--accel", default="tcg")
    p.add_argument("--timeout", type=int, default=60)
    args = p.parse_args()

    results = run_tests(accel=args.accel, timeout=args.timeout)

    print("\n" + "="*60)
    print("TEST RESULTS")
    print("="*60)
    passed = failed = 0
    for name, (status, detail) in results.items():
        mark = "✓" if status == "OK" else "✗"
        print(f"  {mark}  {name:<20} {status}")
        if detail:
            print(f"       {detail[:120]}")
        if status == "OK":
            passed += 1
        else:
            failed += 1
    print(f"\n  {passed} passed, {failed} failed\n")
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    raise SystemExit(main())
