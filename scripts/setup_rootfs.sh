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

echo "[+] Setup complete! Rootfs is ready at $ROOTFS_DIR"
