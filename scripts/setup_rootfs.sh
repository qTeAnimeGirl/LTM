#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOTFS_DIR="$PROJECT_ROOT/rootfs"

echo "=== LTM Linux ARM64 Rootfs Setup ==="
mkdir -p "$ROOTFS_DIR"

if [ -f "$ROOTFS_DIR/bin/busybox" ]; then
    echo "[+] Rootfs already exists at $ROOTFS_DIR"
else
    echo "[+] Downloading Alpine Linux 3.20 aarch64 minirootfs..."
    curl -sSL https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/aarch64/alpine-minirootfs-3.20.0-aarch64.tar.gz | tar -xz -C "$ROOTFS_DIR"
    echo "[+] Alpine rootfs successfully unpacked!"
fi

if [ ! -f "$ROOTFS_DIR/usr/bin/fastfetch" ]; then
    echo "[+] Downloading and installing fastfetch (aarch64)..."
    TMP_FF="$(mktemp -d /tmp/fastfetch.XXXXXX)"
    curl -sSL -o "$TMP_FF/fastfetch.apk" https://dl-cdn.alpinelinux.org/alpine/v3.20/community/aarch64/fastfetch-2.13.1-r0.apk
    tar -xz -C "$TMP_FF" -f "$TMP_FF/fastfetch.apk" 2>/dev/null || true
    mkdir -p "$ROOTFS_DIR/usr/bin" "$ROOTFS_DIR/usr/share"
    cp "$TMP_FF/usr/bin/fastfetch" "$ROOTFS_DIR/usr/bin/"
    cp -r "$TMP_FF/usr/share/fastfetch" "$ROOTFS_DIR/usr/share/" || true
    chmod +x "$ROOTFS_DIR/usr/bin/fastfetch"
    rm -rf "$TMP_FF"
    echo "[+] fastfetch installed at $ROOTFS_DIR/usr/bin/fastfetch"
fi

if [ ! -f "$ROOTFS_DIR/bin/bash" ]; then
    echo "[+] Downloading and installing GNU bash and dependencies (aarch64)..."
    TMP_BASH="$(mktemp -d /tmp/bash.XXXXXX)"
    curl -sSL -o "$TMP_BASH/bash.apk" https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/bash-5.2.26-r0.apk
    curl -sSL -o "$TMP_BASH/readline.apk" https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/readline-8.2.10-r0.apk
    curl -sSL -o "$TMP_BASH/libncursesw.apk" https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/libncursesw-6.4_p20240420-r2.apk
    curl -sSL -o "$TMP_BASH/ncurses-terminfo-base.apk" https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/ncurses-terminfo-base-6.4_p20240420-r2.apk
    tar -xz -C "$ROOTFS_DIR" -f "$TMP_BASH/bash.apk" 2>/dev/null || true
    tar -xz -C "$ROOTFS_DIR" -f "$TMP_BASH/readline.apk" 2>/dev/null || true
    tar -xz -C "$ROOTFS_DIR" -f "$TMP_BASH/libncursesw.apk" 2>/dev/null || true
    tar -xz -C "$ROOTFS_DIR" -f "$TMP_BASH/ncurses-terminfo-base.apk" 2>/dev/null || true
    chmod +x "$ROOTFS_DIR/bin/bash"
    rm -rf "$TMP_BASH"
    echo "[+] GNU bash installed at $ROOTFS_DIR/bin/bash"
fi

echo "[+] Setting up network DNS configuration..."
mkdir -p "$ROOTFS_DIR/etc"
rm -f "$ROOTFS_DIR/etc/resolv.conf"
cat << 'EOF' > "$ROOTFS_DIR/etc/resolv.conf"
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF

echo "[+] Initializing /proc, /sys, and /dev directory nodes..."
chmod 755 "$ROOTFS_DIR/proc" "$ROOTFS_DIR/sys" "$ROOTFS_DIR/dev" 2>/dev/null || true
mkdir -p "$ROOTFS_DIR/proc/self" "$ROOTFS_DIR/proc/sys/kernel" "$ROOTFS_DIR/sys/devices/system/cpu" "$ROOTFS_DIR/sys/class/dmi/id" "$ROOTFS_DIR/dev/pts" "$ROOTFS_DIR/dev/shm"
touch "$ROOTFS_DIR/proc/cpuinfo" "$ROOTFS_DIR/proc/meminfo" "$ROOTFS_DIR/proc/version" "$ROOTFS_DIR/proc/uptime" "$ROOTFS_DIR/proc/loadavg" "$ROOTFS_DIR/proc/mounts" "$ROOTFS_DIR/proc/stat" "$ROOTFS_DIR/proc/filesystems" "$ROOTFS_DIR/proc/sys/kernel/osrelease"
touch "$ROOTFS_DIR/proc/self/cmdline" "$ROOTFS_DIR/proc/self/maps" "$ROOTFS_DIR/proc/self/status" "$ROOTFS_DIR/proc/self/stat" "$ROOTFS_DIR/proc/self/mounts"
touch "$ROOTFS_DIR/sys/devices/system/cpu/online" "$ROOTFS_DIR/sys/devices/system/cpu/present" "$ROOTFS_DIR/sys/devices/system/cpu/possible" "$ROOTFS_DIR/sys/class/dmi/id/product_name" "$ROOTFS_DIR/sys/class/dmi/id/sys_vendor"
touch "$ROOTFS_DIR/dev/null" "$ROOTFS_DIR/dev/zero" "$ROOTFS_DIR/dev/urandom" "$ROOTFS_DIR/dev/random" "$ROOTFS_DIR/dev/tty" "$ROOTFS_DIR/dev/console" "$ROOTFS_DIR/dev/ptmx" "$ROOTFS_DIR/dev/stdin" "$ROOTFS_DIR/dev/stdout" "$ROOTFS_DIR/dev/stderr" "$ROOTFS_DIR/dev/fd"

echo "[+] Setup complete! Rootfs is ready at $ROOTFS_DIR"
