#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

NATIVE_BIN="$PROJECT_ROOT/build/bench_scaling_native"
LINUX_BIN="$PROJECT_ROOT/tests/bin/bench_scaling"
LTM_EXEC="$PROJECT_ROOT/ltm"

# 1. Compile native Darwin binary if needed
if [ ! -f "$NATIVE_BIN" ]; then
    echo "[*] Compiling Darwin Native Scaling Benchmark..."
    mkdir -p "$PROJECT_ROOT/build"
    clang -O3 -pthread "$PROJECT_ROOT/tests/bench_scaling.c" -o "$NATIVE_BIN"
fi

# 2. Compile Linux ARM64 binary if needed
if [ ! -f "$LINUX_BIN" ]; then
    echo "[*] Compiling Linux ARM64 Scaling Benchmark with Glibc..."
    LLD=$(find "$HOME/.rustup" -name "rust-lld" 2>/dev/null | head -n 1)
    rootfs="$PROJECT_ROOT/rootfs"
    src="$PROJECT_ROOT/tests/bench_scaling.c"
    obj="$PROJECT_ROOT/tests/bin/bench_scaling.o"
    clang -target aarch64-linux-gnu -O2 -ffixed-x18 -fstack-protector-strong -isystem "$rootfs/usr/include" -c "$src" -o "$obj"
    "$LLD" -flavor gnu --sysroot="$rootfs" -pie --dynamic-linker /lib/ld-linux-aarch64.so.1 -L"$rootfs/usr/lib" "$rootfs/usr/lib/Scrt1.o" "$rootfs/usr/lib/crti.o" "$obj" -lc -lpthread -lm "$rootfs/usr/lib/crtn.o" -o "$LINUX_BIN"
    rm -f "$obj"
    cp "$LINUX_BIN" "$rootfs/usr/bin/bench_scaling"
fi

# 3. Ensure LTM is built
if [ ! -f "$LTM_EXEC" ]; then
    echo "[*] Building LTM translation runtime..."
    make -C "$PROJECT_ROOT"
fi

echo "=========================================================================================="
echo "         LTM 1-TO-8 CORE CPU SCALING BENCHMARK (GEMM, SIEVE, FIBONACCI, MEMORY)           "
echo "  Apple Silicon M3 Hardware: Native macOS Darwin vs Linux AArch64 Translation Runtime     "
echo "=========================================================================================="

echo ""
echo "[1/2] Executing macOS Darwin Native Scaling Baseline..."
NATIVE_RAW=$("$NATIVE_BIN" "macOS-Darwin-Native")
echo "$NATIVE_RAW" | grep -v "^SCALING_DATA:"
NATIVE_LINE=$(echo "$NATIVE_RAW" | grep "^SCALING_DATA:" | tr -d '\r')

echo ""
echo "[2/2] Executing Linux ARM64 ELF via LTM Translation Layer..."
LTM_RAW=$("$LTM_EXEC" "$LINUX_BIN" "Linux-ARM64-LTM")
echo "$LTM_RAW" | grep -v "^SCALING_DATA:"
LTM_LINE=$(echo "$LTM_RAW" | grep "^SCALING_DATA:" | tr -d '\r')

# Parse CSV lines
# SCALING_DATA:tag:g1:g2:g4:g8:s1:s2:s4:s8:f1:f2:f4:f8:m1:m2:m4:m8
IFS=':' read -r _ n_tag ng1 ng2 ng4 ng8 ns1 ns2 ns4 ns8 nf1 nf2 nf4 nf8 nm1 nm2 nm4 nm8 <<< "$NATIVE_LINE"
IFS=':' read -r _ l_tag lg1 lg2 lg4 lg8 ls1 ls2 ls4 ls8 lf1 lf2 lf4 lf8 lm1 lm2 lm4 lm8 <<< "$LTM_LINE"

calc_eff() {
    local nat="$1"
    local ltm="$2"
    if [ "$ltm" -eq 0 ]; then
        echo "100.0%"
        return
    fi
    awk -v n="$nat" -v l="$ltm" 'BEGIN { printf "%.1f%%", (n / l) * 100 }'
}

calc_delta() {
    local nat="$1"
    local ltm="$2"
    awk -v n="$nat" -v l="$ltm" 'BEGIN { printf "%+d ms", (l - n) }'
}

echo ""
echo "=========================================================================================="
echo "                   SIDE-BY-SIDE MULTI-CORE SCALING COMPARISON MATRIX                      "
echo "=========================================================================================="
printf "%-30s | %-12s | %-12s | %-10s | %-12s\n" "Workload & Core Count" "Native macOS" "Linux (LTM)" "Delta" "Efficiency"
echo "-------------------------------+--------------+--------------+------------+---------------"

printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Matrix GEMM (1 Core)" "$ng1" "$lg1" "$(calc_delta "$ng1" "$lg1")" "$(calc_eff "$ng1" "$lg1")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Matrix GEMM (2 Cores)" "$ng2" "$lg2" "$(calc_delta "$ng2" "$lg2")" "$(calc_eff "$ng2" "$lg2")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Matrix GEMM (4 Cores)" "$ng4" "$lg4" "$(calc_delta "$ng4" "$lg4")" "$(calc_eff "$ng4" "$lg4")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Matrix GEMM (8 Cores)" "$ng8" "$lg8" "$(calc_delta "$ng8" "$lg8")" "$(calc_eff "$ng8" "$lg8")"
echo "-------------------------------+--------------+--------------+------------+---------------"

printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Prime Sieve (1 Core - 10M)" "$ns1" "$ls1" "$(calc_delta "$ns1" "$ls1")" "$(calc_eff "$ns1" "$ls1")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Prime Sieve (2 Cores - 20M)" "$ns2" "$ls2" "$(calc_delta "$ns2" "$ls2")" "$(calc_eff "$ns2" "$ls2")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Prime Sieve (4 Cores - 40M)" "$ns4" "$ls4" "$(calc_delta "$ns4" "$ls4")" "$(calc_eff "$ns4" "$ls4")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Prime Sieve (8 Cores - 80M)" "$ns8" "$ls8" "$(calc_delta "$ns8" "$ls8")" "$(calc_eff "$ns8" "$ls8")"
echo "-------------------------------+--------------+--------------+------------+---------------"

printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Fibonacci F(40) (1 Core)" "$nf1" "$lf1" "$(calc_delta "$nf1" "$lf1")" "$(calc_eff "$nf1" "$lf1")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Fibonacci F(40) (2 Cores)" "$nf2" "$lf2" "$(calc_delta "$nf2" "$lf2")" "$(calc_eff "$nf2" "$lf2")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Fibonacci F(40) (4 Cores)" "$nf4" "$lf4" "$(calc_delta "$nf4" "$lf4")" "$(calc_eff "$nf4" "$lf4")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Fibonacci F(40) (8 Cores)" "$nf8" "$lf8" "$(calc_delta "$nf8" "$lf8")" "$(calc_eff "$nf8" "$lf8")"
echo "-------------------------------+--------------+--------------+------------+---------------"

printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Memory Stream (1 Core - 64M)" "$nm1" "$lm1" "$(calc_delta "$nm1" "$lm1")" "$(calc_eff "$nm1" "$lm1")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Memory Stream (2 Cores - 128M)" "$nm2" "$lm2" "$(calc_delta "$nm2" "$lm2")" "$(calc_eff "$nm2" "$lm2")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Memory Stream (4 Cores - 256M)" "$nm4" "$lm4" "$(calc_delta "$nm4" "$lm4")" "$(calc_eff "$nm4" "$lm4")"
printf "%-30s | %8d ms   | %8d ms   | %10s | %10s\n" "Memory Stream (8 Cores - 512M)" "$nm8" "$lm8" "$(calc_delta "$nm8" "$lm8")" "$(calc_eff "$nm8" "$lm8")"
echo "=========================================================================================="
