#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

NATIVE_BIN="$PROJECT_ROOT/build/test_concurrency_stress_native"
LINUX_BIN="$PROJECT_ROOT/tests/bin/test_concurrency_stress"
LTM_EXEC="$PROJECT_ROOT/ltm"

# 1. Compile native Darwin binary if needed
if [ ! -f "$NATIVE_BIN" ]; then
    echo "[*] Compiling Darwin Native Concurrency Stress Test..."
    mkdir -p "$PROJECT_ROOT/build"
    clang -O3 -pthread "$PROJECT_ROOT/tests/test_concurrency_stress.c" -o "$NATIVE_BIN"
fi

# 2. Compile Linux ARM64 binary if needed
if [ ! -f "$LINUX_BIN" ]; then
    echo "[*] Compiling Linux ARM64 Concurrency Stress Test with Glibc..."
    LLD=$(find "$HOME/.rustup" -name "rust-lld" 2>/dev/null | head -n 1)
    rootfs="$PROJECT_ROOT/rootfs"
    src="$PROJECT_ROOT/tests/test_concurrency_stress.c"
    obj="$PROJECT_ROOT/tests/bin/test_concurrency_stress.o"
    clang -target aarch64-linux-gnu -O2 -ffixed-x18 -fstack-protector-strong -isystem "$rootfs/usr/include" -c "$src" -o "$obj"
    "$LLD" -flavor gnu --sysroot="$rootfs" -pie --dynamic-linker /lib/ld-linux-aarch64.so.1 -L"$rootfs/usr/lib" "$rootfs/usr/lib/Scrt1.o" "$rootfs/usr/lib/crti.o" "$obj" -lc -lpthread -lm "$rootfs/usr/lib/crtn.o" -o "$LINUX_BIN"
    rm -f "$obj"
    cp "$LINUX_BIN" "$rootfs/usr/bin/test_concurrency_stress"
fi

# 3. Ensure LTM is built
if [ ! -f "$LTM_EXEC" ]; then
    echo "[*] Building LTM translation runtime..."
    make -C "$PROJECT_ROOT"
fi

echo "=========================================================================================="
echo "          LTM COMPREHENSIVE CONCURRENCY & STRESS TEST BENCHMARK REPORT                    "
echo "  Target Architecture: Apple Silicon M3 (Darwin Mach-O vs Unmodified Linux AArch64 ELF)   "
echo "=========================================================================================="

echo ""
echo "[1/2] Running Native macOS Darwin Baseline..."
"$NATIVE_BIN"

echo ""
echo "[2/2] Running Linux ARM64 ELF via LTM Translation Engine..."
"$LTM_EXEC" "$LINUX_BIN"

echo ""
echo "=========================================================================================="
echo "[+] Concurrency and stress benchmarks completed successfully across both targets!"
echo "=========================================================================================="
