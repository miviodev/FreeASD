#!/usr/bin/env python3
"""OpenASD automated test runner — avoids Cyrillic paths."""
import subprocess, time, sys, threading, re, os, shutil

QEMU      = r"C:\msys64\ucrt64\bin\qemu-system-x86_64.exe"
OVMF_CODE = r"C:\msys64\ucrt64\share\qemu\edk2-x86_64-code.fd"
OVMF_VARS = r"C:\asd_run\OVMF_VARS.fd"
LIVE      = r"C:\asd_run\live.img"

cmd = [
    QEMU,
    "-machine", "q35,accel=tcg",
    "-cpu", "qemu64", "-m", "512",
    "-display", "none", "-no-reboot",
    "-serial", "stdio",
    "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
    "-drive", f"if=pflash,format=raw,file={OVMF_VARS}",
    "-drive", f"file={LIVE},if=none,id=live0,format=raw",
    "-device", "virtio-blk-pci,drive=live0,bootindex=1",
    "-netdev", "user,id=net0,net=10.0.2.0/24,host=10.0.2.2",
    "-device", "virtio-net-pci,netdev=net0",
]
print("[qemu]", cmd[0], "...", flush=True)

p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.STDOUT, bufsize=0)

buf = b""
lock = threading.Lock()

def reader():
    global buf
    while True:
        try:
            data = p.stdout.read(256)
        except Exception:
            break
        if not data:
            break
        with lock:
            buf += data
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()

threading.Thread(target=reader, daemon=True).start()

def wait_for(pat, timeout=60):
    rx = re.compile(pat if isinstance(pat, bytes) else pat.encode(), re.DOTALL)
    deadline = time.time() + timeout
    while time.time() < deadline:
        with lock:
            if rx.search(buf):
                return True
        if p.poll() is not None:
            return False
        time.sleep(0.1)
    return False

def send(s):
    sys.stdout.write(f"\n>>> {s!r}\n"); sys.stdout.flush()
    try:
        p.stdin.write((s + "\n").encode())
        p.stdin.flush()
    except Exception:
        pass

# ── Boot ──────────────────────────────────────────────────────────────────
print("[*] Waiting for boot (90s)...", flush=True)
if not wait_for(rb"login:", 90):
    with lock: print(f"\nBoot output:\n{buf[-3000:].decode('utf-8','replace')}")
    print("TIMEOUT: no login prompt"); p.terminate(); sys.exit(1)

time.sleep(0.3)
send("root")
time.sleep(0.5)
if wait_for(rb"Password:", 5):
    send("")
time.sleep(0.5)
if not wait_for(rb"[#$>]", 15):
    send("")
    wait_for(rb"[#$>]", 10)
print("\n=== SHELL OK ===", flush=True)

results = {}

def run_test(name, cmd_str, ok_pat, fail_pat=None, timeout=30):
    with lock:
        start = len(buf)
    send(cmd_str)
    combined = f"({ok_pat}" + (f"|{fail_pat}" if fail_pat else "") + r"|[#$>]\s*$)"
    wait_for(combined.encode(), timeout)
    wait_for(rb"[#$>]", 8)
    with lock:
        seg = buf[start:].decode("utf-8", errors="replace")
    ok = bool(re.search(ok_pat, seg))
    fail_hit = fail_pat and bool(re.search(fail_pat, seg))
    results[name] = (ok, seg[-500:].strip())
    status = "PASS" if ok else ("FAIL(explicit)" if fail_hit else "FAIL")
    print(f"  [{status}] {name}", flush=True)
    if not ok:
        print(f"     -> {seg[-250:].strip()!r}", flush=True)
    return ok

# ── Test sequence ──────────────────────────────────────────────────────────
run_test("mkdir_tmp",   "mkdir /tmp",           r"[#$>]")
run_test("touch_file",  "touch /tmp/hello.txt", r"[#$>]")
run_test("ls_tmp",      "ls /tmp",              r"hello\.txt",  r"cannot open")
run_test("filetest",    "filetest",             r"FILETEST OK", r"FILETEST FAIL", timeout=30)
run_test("ping_gw",     "ping 10.0.2.2",        r"bytes from",  r"100% packet loss", timeout=50)

send("poweroff")
time.sleep(4)
if p.poll() is None:
    p.terminate()

# ── Summary ────────────────────────────────────────────────────────────────
print("\n" + "="*55)
print("TEST RESULTS")
print("="*55)
passed = failed = 0
for name, (ok, detail) in results.items():
    print(f"  {'PASS' if ok else 'FAIL'}  {name}")
    if not ok:
        print(f"       {detail[:200]!r}")
    if ok: passed += 1
    else: failed += 1
print(f"\n  {passed} passed, {failed} failed")
print("="*55)
sys.exit(0 if failed == 0 else 1)
