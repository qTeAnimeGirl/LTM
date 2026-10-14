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

echo "[+] Setup complete! Rootfs is ready at $ROOTFS_DIR"
