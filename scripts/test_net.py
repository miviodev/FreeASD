#!/usr/bin/env python3
"""
test_net.py — OpenASD network autotest driver.

Builds kernel, patches disk image with autotest_nettest cmdline,
runs QEMU headlessly with virtio-net, reads serial log, reports results.

Usage (from repo root, in Git Bash):
    python3 scripts/test_net.py
    python3 scripts/test_net.py --no-build   # skip make
    python3 scripts/test_net.py --timeout 60
"""

import argparse, os, subprocess, sys, time, glob, shutil

# ── Repo layout ──────────────────────────────────────────────────────────────
REPO      = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD_RUN = os.path.join(REPO, "build", "run")
BUILD_DBG = os.path.join(REPO, "build", "debug")
DISK_IMG  = os.path.join(BUILD_RUN, "disk.img")
SERIAL    = os.path.join(BUILD_DBG, "serial.log")
NETCONF   = os.path.join(BUILD_RUN, "asdboot-nettest.conf")

ASDBOOT_CONF = """\
timeout = 0

menu
  title = "ASD Boot"
  default = "default"
end

entry
  id = "default"
  label = "ASD Kernel"
  kernel = "/boot/asdkernel.bin"
  cmdline = "autotest_nettest"
end
"""

ASDBOOT_NORMAL = """\
timeout = 5

menu
  title = "ASD Boot"
  default = "default"
end

entry
  id = "default"
  label = "ASD OpenASD"
  kernel = "/boot/asdkernel.bin"
  cmdline = ""
end
"""


def find_qemu():
    for name in ("qemu-system-x86_64", "qemu-system-x86_64.exe"):
        p = shutil.which(name)
        if p:
            return p
    # Common Windows install paths
    for p in (
        r"C:\Program Files\qemu\qemu-system-x86_64.exe",
        r"C:\Program Files (x86)\qemu\qemu-system-x86_64.exe",
    ):
        if os.path.isfile(p):
            return p
    return None


def find_ovmf():
    """Return (OVMF_CODE, OVMF_VARS) paths or (None, None)."""
    candidates = [
        ("/usr/share/OVMF/OVMF_CODE.fd",        "/usr/share/OVMF/OVMF_VARS.fd"),
        ("/usr/share/ovmf/OVMF.fd",             "/usr/share/ovmf/OVMF.fd"),
        ("/usr/share/edk2/ovmf/OVMF_CODE.fd",   "/usr/share/edk2/ovmf/OVMF_VARS.fd"),
    ]
    # Also check BUILD_RUN for locally downloaded OVMF
    for f in glob.glob(os.path.join(BUILD_RUN, "OVMF_CODE*")):
        var = f.replace("CODE", "VARS")
        if os.path.isfile(var):
            candidates.insert(0, (f, var))

    for code, var in candidates:
        if os.path.isfile(code) and os.path.isfile(var):
            return code, var
    return None, None


def run_cmd(cmd, cwd=REPO, check=True):
    print(f"  $ {' '.join(cmd)}", flush=True)
    r = subprocess.run(cmd, cwd=cwd, check=check)
    return r.returncode


def patch_disk(disk, conf_text, path_on_disk):
    """Write conf_text into the FAT32 disk image at path_on_disk via mcopy."""
    tmp = NETCONF
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    with open(tmp, "w", newline="\n") as f:
        f.write(conf_text)
    # mcopy -o overwrites
    subprocess.run(
        ["mcopy", "-o", "-i", disk, tmp, f"::{path_on_disk}"],
        check=True, capture_output=True
    )


def run_qemu(qemu, disk, ovmf_code, ovmf_vars_bfd, timeout):
    os.makedirs(BUILD_DBG, exist_ok=True)
    if os.path.exists(SERIAL):
        os.remove(SERIAL)

    cmd = [
        qemu,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64",
        "-m", "1024",
        "-display", "none",
        "-no-reboot",
        "-serial", f"file:{SERIAL}",
        "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={ovmf_vars_bfd}",
        "-drive", f"file={disk},if=none,id=inst0,format=raw",
        "-device", "virtio-blk-pci,drive=inst0,bootindex=1",
        "-netdev", "user,id=net0,net=10.0.2.0/24,host=10.0.2.2,dns=10.0.2.3",
        "-device", "virtio-net-pci,netdev=net0",
    ]
    print(f"\n  Running QEMU (timeout={timeout}s)...", flush=True)
    try:
        subprocess.run(cmd, timeout=timeout, cwd=REPO,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        print("  (QEMU timed out — that's expected for headless autotest)")


def read_serial():
    try:
        with open(SERIAL, "r", errors="replace") as f:
            return f.read()
    except FileNotFoundError:
        return ""


def analyse(log):
    """Parse serial log, return (passed, failed, notes)."""
    passed, failed, notes = [], [], []

    lines = log.splitlines()
    for line in lines:
        if "NETTEST PING OK"   in line: passed.append("ping")
        if "NETTEST PING FAIL" in line: failed.append("ping")
        if "NETTEST DNS OK"    in line: passed.append("dns")
        if "NETTEST DNS FAIL"  in line: failed.append("dns")
        if "NETTEST TCP OK"    in line: passed.append("tcp")
        if "NETTEST TCP FAIL"  in line: failed.append("tcp")
        # kernel debug lines
        if "[DNS]" in line or "[UDP]" in line:
            notes.append(line)

    return passed, failed, notes


def main():
    ap = argparse.ArgumentParser(description="OpenASD network autotest")
    ap.add_argument("--no-build",  action="store_true", help="skip make all")
    ap.add_argument("--timeout",   type=int, default=45, help="QEMU timeout (s)")
    ap.add_argument("--install",   action="store_true", help="run make install first")
    args = ap.parse_args()

    # ── Sanity checks ────────────────────────────────────────────────────────
    qemu = find_qemu()
    if not qemu:
        print("ERROR: qemu-system-x86_64 not found. Install QEMU first.")
        sys.exit(1)
    print(f"QEMU: {qemu}")

    ovmf_code, ovmf_vars = find_ovmf()
    if not ovmf_code:
        print("ERROR: OVMF firmware not found.")
        print("  Linux:   sudo apt install ovmf  (Ubuntu)")
        print("  Windows: download from https://github.com/retrage/edk2-nightly")
        sys.exit(1)
    print(f"OVMF: {ovmf_code}")

    # ── Build ────────────────────────────────────────────────────────────────
    if not args.no_build:
        print("\n[1/4] Building kernel + userland...")
        run_cmd(["make", "all"])

    if args.install or not os.path.isfile(DISK_IMG):
        print("\n[1b] Running make install...")
        run_cmd(["make", "install"])

    if not os.path.isfile(DISK_IMG):
        print(f"ERROR: disk image not found: {DISK_IMG}")
        print("  Run:  make install")
        sys.exit(1)

    # ── OVMF vars copy ───────────────────────────────────────────────────────
    ovmf_vars_bfd = os.path.join(BUILD_RUN, "OVMF_VARS_bfd.fd")
    if not os.path.isfile(ovmf_vars_bfd):
        shutil.copy(ovmf_vars, ovmf_vars_bfd)
        print(f"Copied OVMF_VARS → {ovmf_vars_bfd}")

    # ── Patch disk with autotest_nettest cmdline ─────────────────────────────
    print("\n[2/4] Patching disk image with autotest_nettest cmdline...")
    try:
        patch_disk(DISK_IMG, ASDBOOT_CONF, "EFI/BOOT/asdboot.conf")
        patch_disk(DISK_IMG, ASDBOOT_CONF, "boot/asdboot.conf")
    except subprocess.CalledProcessError as e:
        print(f"ERROR: mcopy failed: {e}")
        print("  Make sure mtools is installed: sudo apt install mtools")
        sys.exit(1)

    # ── Run QEMU ─────────────────────────────────────────────────────────────
    print(f"\n[3/4] Running QEMU (nettest, timeout={args.timeout}s)...")
    run_qemu(qemu, DISK_IMG, ovmf_code, ovmf_vars_bfd, args.timeout)

    # ── Restore normal boot conf ──────────────────────────────────────────────
    try:
        patch_disk(DISK_IMG, ASDBOOT_NORMAL, "EFI/BOOT/asdboot.conf")
        patch_disk(DISK_IMG, ASDBOOT_NORMAL, "boot/asdboot.conf")
    except Exception:
        pass  # best-effort

    # ── Parse results ────────────────────────────────────────────────────────
    print("\n[4/4] Analysing serial log...\n")
    log = read_serial()
    if not log:
        print("ERROR: serial.log is empty — QEMU may have crashed at boot.")
        print(f"  Check: {SERIAL}")
        sys.exit(1)

    # Print relevant lines
    print("=== Relevant serial output ===")
    for line in log.splitlines():
        if any(k in line for k in (
            "NETTEST", "[DNS]", "[UDP]", "[autotest]",
            "EXCEPTION", "virtio", "net:", "ARP",
        )):
            print(f"  {line}")
    print("==============================\n")

    passed, failed, notes = analyse(log)

    print(f"Results:  PASS={passed}  FAIL={failed}")
    if notes:
        print("Kernel debug output:")
        for n in notes:
            print(f"  {n}")

    if "NETTEST ALL OK" in log:
        print("\n✓ ALL NETWORK TESTS PASSED")
        return 0
    elif failed:
        print(f"\n✗ FAILED: {failed}")
        # Give specific hints
        if "ping" in failed:
            print("  Hint: ping failed → virtio-net driver or ARP broken")
        if "dns" in failed and "ping" not in failed:
            print("  Hint: ping works but DNS fails → check [DNS]/[UDP] lines above")
        if "tcp" in failed and "dns" not in failed:
            print("  Hint: DNS works but TCP fails → TCP state machine issue")
        return 1
    else:
        print("\n? No NETTEST result found — did the OS boot?")
        print(f"  Full log: {SERIAL}")
        # Print last 20 lines of serial log
        lines = log.splitlines()
        print(f"\nLast {min(20, len(lines))} lines of serial.log:")
        for line in lines[-20:]:
            print(f"  {line}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
