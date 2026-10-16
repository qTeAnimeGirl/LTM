#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN_DIR="$SCRIPT_DIR/bin"
INCLUDE_DIR="$SCRIPT_DIR/include"

mkdir -p "$BIN_DIR"

echo "=== LTM Test Suite Compiler (Linux ARM64 ELF) ==="

# 1. Locate Clang
CLANG=""
if command -v clang >/dev/null 2>&1; then
    CLANG="$(command -v clang)"
elif [ -f "/opt/homebrew/opt/llvm/bin/clang" ]; then
    CLANG="/opt/homebrew/opt/llvm/bin/clang"
else
    echo "[-] Error: clang compiler not found!" >&2
    exit 1
fi
echo "[+] Using Clang: $CLANG ($($CLANG --version | head -n 1))"

# 2. Locate rust-lld
LLD=""
if command -v rust-lld >/dev/null 2>&1; then
    LLD="$(command -v rust-lld)"
else
    RUSTUP_TOOLCHAIN="$(rustup toolchain list 2>/dev/null | grep default | awk '{print $1}')"
    if [ -z "$RUSTUP_TOOLCHAIN" ]; then
        RUSTUP_TOOLCHAIN="stable-aarch64-apple-darwin"
    fi
    CANDIDATE="$HOME/.rustup/toolchains/$RUSTUP_TOOLCHAIN/lib/rustlib/aarch64-apple-darwin/bin/rust-lld"
    if [ -f "$CANDIDATE" ]; then
        LLD="$CANDIDATE"
    else
        FOUND_LLD="$(find "$HOME/.rustup" -name "rust-lld" 2>/dev/null | head -n 1 || true)"
        if [ -n "$FOUND_LLD" ]; then
            LLD="$FOUND_LLD"
        fi
    fi
fi

if [ -z "$LLD" ] || [ ! -x "$LLD" ]; then
    echo "[-] Error: rust-lld linker not found in ~/.rustup or PATH!" >&2
    exit 1
fi
echo "[+] Using LLD: $LLD"

CFLAGS="-target aarch64-linux-gnu -O2 -ffreestanding -nostdlib -ffixed-x18 -I$SCRIPT_DIR -I$INCLUDE_DIR"

build_asm() {
    local src="$1"
    local name="$(basename "$src" .s)"
    local obj="$BIN_DIR/$name.o"
    local out="$BIN_DIR/$name"

    echo "  -> Assembling & linking $name.s..."
    "$CLANG" -target aarch64-linux-gnu -c "$src" -o "$obj"
    "$LLD" -flavor gnu -static --image-base=0x400000000 "$obj" -o "$out"
    rm -f "$obj"
}

build_c() {
    local src="$1"
    local name="$(basename "$src" .c)"
    local out_dir="$BIN_DIR"
    if [[ "$src" == *"/syscalls/"* ]]; then
        out_dir="$BIN_DIR/syscalls"
        mkdir -p "$out_dir"
    fi
    local obj="$out_dir/$name.o"
    local out="$out_dir/$name"

    echo "  -> Compiling & linking $name.c..."
    "$CLANG" $CFLAGS -c "$src" -o "$obj"
    "$LLD" -flavor gnu -static --image-base=0x400000000 "$obj" -o "$out"
    rm -f "$obj"
}

build_glibc() {
    local src="$1"
    local name="$(basename "$src" .c)"
    local out="$BIN_DIR/$name"
    local obj="$BIN_DIR/$name.o"
    local rootfs="$PROJECT_ROOT/rootfs"

    if [ -f "$rootfs/usr/lib/libc.so" ] && [ -f "$rootfs/usr/lib/Scrt1.o" ]; then
        echo "  -> Compiling & dynamic linking $name.c with Glibc..."
        local fixed_flag="-ffixed-x18"
        if [ "$name" = "test_x18_virtualization" ]; then
            fixed_flag=""
        fi
        "$CLANG" -target aarch64-linux-gnu -O2 $fixed_flag -fstack-protector-strong -isystem "$rootfs/usr/include" -c "$src" -o "$obj"
        "$LLD" -flavor gnu --sysroot="$rootfs" -pie --dynamic-linker /lib/ld-linux-aarch64.so.1 -L"$rootfs/usr/lib" "$rootfs/usr/lib/Scrt1.o" "$rootfs/usr/lib/crti.o" "$obj" -lc -lpthread -lm "$rootfs/usr/lib/crtn.o" -o "$out"
        rm -f "$obj"
        cp "$out" "$rootfs/usr/bin/$name"
        if [ -d "$PROJECT_ROOT/arch_rootfs/usr/bin" ]; then
            cp "$out" "$PROJECT_ROOT/arch_rootfs/usr/bin/$name"
        fi
    fi
}

# Build Assembly tests
for asm_file in "$SCRIPT_DIR"/*.s; do
    if [ -f "$asm_file" ]; then
        build_asm "$asm_file"
    fi
done

# Build C tests
for c_file in "$SCRIPT_DIR"/*.c; do
    if [ -f "$c_file" ]; then
        c_name="$(basename "$c_file" .c)"
        if [ "$c_name" = "test_glibc" ] || [ "$c_name" = "test_concurrency_stress" ] || [ "$c_name" = "bench_scaling" ] || [ "$c_name" = "test_x18_virtualization" ]; then
            build_glibc "$c_file"
        elif [ "$c_name" != "syscall_test" ]; then
            build_c "$c_file"
        fi
    fi
done

# Build Standalone Syscall tests (Sprint 10)
if [ -d "$SCRIPT_DIR/syscalls" ]; then
    for c_file in "$SCRIPT_DIR/syscalls"/*.c; do
        if [ -f "$c_file" ]; then
            build_c "$c_file"
        fi
    done
fi

echo "[+] Successfully built all Linux ARM64 test binaries into $BIN_DIR"
ls -la "$BIN_DIR"
