#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
TARGET_DIR="${1:-$PROJECT_ROOT/arch_rootfs}"

echo "=============================================================="
echo "   LTM Arch Linux ARM64 Rootfs & Environment Setup"
echo "=============================================================="
echo "[*] Target directory: $TARGET_DIR"

mkdir -p "$TARGET_DIR"

if [ -f "$TARGET_DIR/bin/sh" ] || [ -f "$TARGET_DIR/bin/bash" ]; then
    echo "[+] Arch Linux ARM rootfs already present at $TARGET_DIR"
else
    echo "[+] Downloading Arch Linux ARM aarch64 rootfs archive..."
    TMP_ARCHIVE="$(mktemp /tmp/archlinux_arm.XXXXXX.tar.gz)"
    curl -L --progress-bar -o "$TMP_ARCHIVE" "http://os.archlinuxarm.org/os/ArchLinuxARM-aarch64-latest.tar.gz"
    
    echo "[+] Extracting Arch Linux ARM rootfs into $TARGET_DIR..."
    tar -xzf "$TMP_ARCHIVE" -C "$TARGET_DIR" 2>/dev/null || tar -xf "$TMP_ARCHIVE" -C "$TARGET_DIR"
    rm -f "$TMP_ARCHIVE"
    echo "[+] Arch Linux ARM rootfs extracted successfully!"
fi

echo "[+] Setting up network DNS configuration in /etc/resolv.conf..."
mkdir -p "$TARGET_DIR/etc"
rm -f "$TARGET_DIR/etc/resolv.conf"
cat << 'EOF' > "$TARGET_DIR/etc/resolv.conf"
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF

if [ -f "$TARGET_DIR/etc/pacman.conf" ]; then
    echo "[+] Configuring /etc/pacman.conf for LTM translation environment..."
    # Disable alpm download user
    sed -i.bak 's/^[[:space:]]*DownloadUser[[:space:]]*=[[:space:]]*alpm/#DownloadUser = alpm/' "$TARGET_DIR/etc/pacman.conf" 2>/dev/null || true
    
    # Ensure DisableSandboxFilesystem and DisableSandboxSyscalls are present
    if ! grep -q "DisableSandboxFilesystem" "$TARGET_DIR/etc/pacman.conf"; then
        sed -i.bak '/\[options\]/a \
DisableSandboxFilesystem\
DisableSandboxSyscalls
' "$TARGET_DIR/etc/pacman.conf" 2>/dev/null || true
    fi
    
    # Set SigLevel = Never so pacman works cleanly without requiring prior pacman-key --init
    sed -i.bak 's/^[[:space:]]*SigLevel[[:space:]]*=.*/SigLevel = Never/' "$TARGET_DIR/etc/pacman.conf" 2>/dev/null || true
    rm -f "$TARGET_DIR/etc/pacman.conf.bak"
fi

echo "[+] Initializing /proc, /sys, and /dev directory nodes..."
chmod 755 "$TARGET_DIR/proc" "$TARGET_DIR/sys" "$TARGET_DIR/dev" 2>/dev/null || true
mkdir -p "$TARGET_DIR/proc/self" "$TARGET_DIR/proc/sys/kernel" "$TARGET_DIR/sys/devices/system/cpu" "$TARGET_DIR/sys/class/dmi/id" "$TARGET_DIR/dev/pts" "$TARGET_DIR/dev/shm"
touch "$TARGET_DIR/proc/cpuinfo" "$TARGET_DIR/proc/meminfo" "$TARGET_DIR/proc/version" "$TARGET_DIR/proc/uptime" "$TARGET_DIR/proc/loadavg" "$TARGET_DIR/proc/mounts" "$TARGET_DIR/proc/stat" "$TARGET_DIR/proc/filesystems" "$TARGET_DIR/proc/sys/kernel/osrelease" 2>/dev/null || true
touch "$TARGET_DIR/proc/self/cmdline" "$TARGET_DIR/proc/self/maps" "$TARGET_DIR/proc/self/status" "$TARGET_DIR/proc/self/stat" "$TARGET_DIR/proc/self/mounts" 2>/dev/null || true
touch "$TARGET_DIR/sys/devices/system/cpu/online" "$TARGET_DIR/sys/devices/system/cpu/present" "$TARGET_DIR/sys/devices/system/cpu/possible" "$TARGET_DIR/sys/class/dmi/id/product_name" "$TARGET_DIR/sys/class/dmi/id/sys_vendor" 2>/dev/null || true
touch "$TARGET_DIR/dev/null" "$TARGET_DIR/dev/zero" "$TARGET_DIR/dev/urandom" "$TARGET_DIR/dev/random" "$TARGET_DIR/dev/tty" "$TARGET_DIR/dev/console" "$TARGET_DIR/dev/ptmx" "$TARGET_DIR/dev/stdin" "$TARGET_DIR/dev/stdout" "$TARGET_DIR/dev/stderr" "$TARGET_DIR/dev/fd" 2>/dev/null || true

echo "=============================================================="
echo "   Arch Linux ARM Rootfs Ready at $TARGET_DIR!"
echo "=============================================================="
