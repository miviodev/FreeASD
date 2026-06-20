#!/usr/bin/env python3
import sys
import os
import re
import time
import subprocess

def main():
    cwd = "C:\\Users\\Администратор\\Downloads\\OpenASD-main"
    qemu = "qemu-system-x86_64"
    ovmf_code = "C:\\msys64\\ucrt64\\share\\qemu\\edk2-x86_64-code.fd"
    ovmf_vars = "build/run/OVMF_VARS.fd"
    live_img = "build/run/live.img"
    disk_img = "build/disk/asd-target.img"

    cmd = [
        qemu,
        "-machine", "q35,accel=tcg",
        "-cpu", "qemu64",
        "-m", "1024",
        "-display", "none",
        "-no-reboot",
        "-serial", "stdio",
        "-drive", f"if=pflash,format=raw,readonly=on,file={ovmf_code}",
        "-drive", f"if=pflash,format=raw,file={ovmf_vars}",
        "-drive", f"file={live_img},if=none,id=live0,format=raw",
        "-device", "virtio-blk-pci,drive=live0,bootindex=1",
        "-drive", f"file={disk_img},if=none,id=inst0,format=raw",
        "-device", "virtio-blk-pci,drive=inst0,bootindex=2"
    ]

    print("Running command:", " ".join(cmd))
    
    # Run QEMU process. Setting creationflags or avoiding socket modules
    # inside selectors helps avoid the WSAStartup error in native Python.
    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        cwd=cwd,
        bufsize=0
    )

    # Use basic non-blocking read with sleep instead of sockets/selectors
    # to avoid WSAStartup Winsock issues on Windows inside this runner environment
    os.set_blocking(proc.stdout.fileno(), False)

    output = []
    
    def read_until(pattern, timeout=25.0):
        deadline = time.monotonic() + timeout
        rx = re.compile(pattern)
        
        while time.monotonic() < deadline:
            try:
                chunk = proc.stdout.read(4096)
                if chunk:
                    decoded = chunk.decode("utf-8", errors="replace")
                    sys.stdout.write(decoded)
                    sys.stdout.flush()
                    output.append(decoded)
            except BlockingIOError:
                pass
                
            text = "".join(output)
            m = rx.search(text)
            if m:
                return m
            
            if proc.poll() is not None:
                raise RuntimeError(f"QEMU exited early with status {proc.returncode}")
                
            time.sleep(0.05)
        raise TimeoutError(f"Timed out waiting for pattern: {pattern}")

    try:
        print("\n--- Phase 1: Waiting for boot menu ---")
        read_until("Press I to install or S for live shell:", timeout=30.0)
        
        print("\n--- Phase 2: Choosing live shell (sending 'S') ---")
        proc.stdin.write(b"S\n")
        proc.stdin.flush()
        
        print("\n--- Phase 3: Waiting for execution ---")
        read_until(r"\[autotest\] OK", timeout=25.0)

        fastfetch_out = "".join(output)

        print("\n--- Phase 4: Shutting down ---")
        proc.terminate()
        proc.wait(timeout=10.0)
        print("\nQEMU exited cleanly.")

        # Validation assertions
        assert "OpenASD fastfetch" in fastfetch_out, "fastfetch title missing"
        assert "Uptime:" in fastfetch_out, "fastfetch output incomplete (missing Uptime)"
        assert "Machine:" in fastfetch_out, "fastfetch output incomplete (missing Machine)"
        assert "*** EXCEPTION" not in fastfetch_out, "Exception found in fastfetch output!"
        
        print("\nAUTO-TEST PASSED SUCCESSFULLY!")
        return 0

    except Exception as e:
        print(f"\nAUTO-TEST FAILED: {e}")
        if proc.poll() is None:
            proc.terminate()
            proc.wait()
        return 1

if __name__ == "__main__":
    sys.exit(main())
