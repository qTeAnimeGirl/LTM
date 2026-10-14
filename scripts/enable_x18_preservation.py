#!/usr/bin/env python3
"""
scripts/enable_x18_preservation.py

Enables kernel-level x18 register preservation for Apple Silicon Darwin XNU.
Under Apple's XNU kernel (osfmk/arm64/machine_task.c):
    if (platform == PLATFORM_MACOS && sdk < sdk_version(13, 0, 0)) {
        task->preserve_x18 = true;
    }
By ensuring the Mach-O LC_BUILD_VERSION sdk field is set to 12.3.0 (0x000c0300),
the XNU kernel natively preserves x18 across all context switches, timer interrupts,
and signal returns without requiring private entitlements or binary patching of Linux ELF binaries.
"""

import sys
import struct
import subprocess

def patch_macho_sdk(binary_path, target_sdk=0x000c0300):
    with open(binary_path, "r+b") as f:
        data = f.read()
        idx = 0
        patched = False
        while True:
            pos = data.find(struct.pack("<I", 0x32), idx) # LC_BUILD_VERSION = 0x32
            if pos == -1:
                break
            if pos + 20 <= len(data):
                platform = struct.unpack("<I", data[pos+8:pos+12])[0]
                if platform == 1: # PLATFORM_MACOS
                    cur_sdk = struct.unpack("<I", data[pos+16:pos+20])[0]
                    f.seek(pos + 16)
                    f.write(struct.pack("<I", target_sdk))
                    patched = True
                    break
            idx = pos + 4

    if not patched:
        print(f"[-] Could not find LC_BUILD_VERSION in {binary_path}", file=sys.stderr)
        return False

    # Re-sign the Mach-O binary ad-hoc
    res = subprocess.run(["codesign", "-s", "-", "-f", binary_path], capture_output=True)
    if res.returncode != 0:
        print(f"[-] codesign failed: {res.stderr.decode()}", file=sys.stderr)
        return False

    return True

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: enable_x18_preservation.py <mach-o-binary>")
        sys.exit(1)
    if patch_macho_sdk(sys.argv[1]):
        print(f"[+] Successfully enabled Darwin XNU x18 preservation on {sys.argv[1]}")
    else:
        sys.exit(1)
