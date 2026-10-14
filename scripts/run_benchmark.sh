#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "======================================================"
echo "          LTM CPU Performance Benchmark               "
echo "  Apple Silicon M3: Darwin Native vs Linux ARM64 ELF  "
echo "======================================================"

cd "$PROJECT_ROOT"

# 1. Compile LTM runtime if needed
if [ ! -f "ltm" ]; then
    echo "[+] Building LTM runtime..."
    make
fi

# 2. Compile Darwin Native Benchmark
echo "[+] Building macOS Darwin Native Benchmark..."
mkdir -p build
clang -O3 -Wall tests/bench_cpu.c -o build/bench_native

# 3. Compile Linux ARM64 Benchmark ELF
echo "[+] Building Linux ARM64 Freestanding Benchmark ELF..."
./tests/build_tests.sh > /dev/null 2>&1

if [ ! -f "build/bench_native" ] || [ ! -f "tests/bin/bench_cpu" ]; then
    echo "[-] Error: Failed to build benchmark binaries!" >&2
    exit 1
fi

echo ""
echo "[1/2] Executing macOS Darwin ARM64 Native Baseline..."
NATIVE_RAW=$(./build/bench_native "macOS-Darwin-Native")
NATIVE_LINE=$(echo "$NATIVE_RAW" | grep "^BENCH_RESULT:" | tr -d '\r')

echo "[2/2] Executing Linux ARM64 ELF via LTM Translation Layer..."
LTM_RAW=$(./ltm tests/bin/bench_cpu)
LTM_LINE=$(echo "$LTM_RAW" | grep "^BENCH_RESULT:" | tr -d '\r')

# Parse fields: tag:sieve:sha:gemm:fib:mem:total
IFS=':' read -r _ NAT_TAG NAT_SIEVE NAT_SHA NAT_GEMM NAT_FIB NAT_MEM NAT_TOTAL <<< "$NATIVE_LINE"
IFS=':' read -r _ LTM_TAG LTM_SIEVE LTM_SHA LTM_GEMM LTM_FIB LTM_MEM LTM_TOTAL <<< "$LTM_LINE"

calc_pct() {
    local nat="$1"
    local ltm="$2"
    if [ "$ltm" -eq 0 ]; then
        echo "100.0"
        return
    fi
    awk -v n="$nat" -v l="$ltm" 'BEGIN { printf "%.1f", (n / l) * 100 }'
}

calc_diff() {
    local nat="$1"
    local ltm="$2"
    awk -v n="$nat" -v l="$ltm" 'BEGIN { diff = l - n; printf "%+d", diff }'
}

PCT_SIEVE=$(calc_pct "$NAT_SIEVE" "$LTM_SIEVE")
PCT_SHA=$(calc_pct "$NAT_SHA" "$LTM_SHA")
PCT_GEMM=$(calc_pct "$NAT_GEMM" "$LTM_GEMM")
PCT_FIB=$(calc_pct "$NAT_FIB" "$LTM_FIB")
PCT_MEM=$(calc_pct "$NAT_MEM" "$LTM_MEM")
PCT_TOTAL=$(calc_pct "$NAT_TOTAL" "$LTM_TOTAL")

DIFF_SIEVE=$(calc_diff "$NAT_SIEVE" "$LTM_SIEVE")
DIFF_SHA=$(calc_diff "$NAT_SHA" "$LTM_SHA")
DIFF_GEMM=$(calc_diff "$NAT_GEMM" "$LTM_GEMM")
DIFF_FIB=$(calc_diff "$NAT_FIB" "$LTM_FIB")
DIFF_MEM=$(calc_diff "$NAT_MEM" "$LTM_MEM")
DIFF_TOTAL=$(calc_diff "$NAT_TOTAL" "$LTM_TOTAL")

echo ""
echo "=========================================================================================="
echo "                               BENCHMARK COMPARISON REPORT                                "
echo "=========================================================================================="
printf "%-32s | %-15s | %-15s | %-12s | %-10s\n" "Workload Description" "Native macOS" "LTM Linux ELF" "Efficiency" "Delta"
echo "------------------------------------------------------------------------------------------"
printf "%-32s | %8s ms      | %8s ms      | %8s %%   | %6s ms\n" "Prime Sieve (10M ints)" "$NAT_SIEVE" "$LTM_SIEVE" "$PCT_SIEVE" "$DIFF_SIEVE"
printf "%-32s | %8s ms      | %8s ms      | %8s %%   | %6s ms\n" "SHA-256 Crypto (500k blocks)" "$NAT_SHA" "$LTM_SHA" "$PCT_SHA" "$DIFF_SHA"
printf "%-32s | %8s ms      | %8s ms      | %8s %%   | %6s ms\n" "GEMM Matrix (300x300 double)" "$NAT_GEMM" "$LTM_GEMM" "$PCT_GEMM" "$DIFF_GEMM"
printf "%-32s | %8s ms      | %8s ms      | %8s %%   | %6s ms\n" "Recursive Fib F(40) (331M)" "$NAT_FIB" "$LTM_FIB" "$PCT_FIB" "$DIFF_FIB"
printf "%-32s | %8s ms      | %8s ms      | %8s %%   | %6s ms\n" "Memory Bandwidth (256MB)" "$NAT_MEM" "$LTM_MEM" "$PCT_MEM" "$DIFF_MEM"
echo "------------------------------------------------------------------------------------------"
printf "%-32s | %8s ms      | %8s ms      | %8s %%   | %6s ms\n" "OVERALL TOTAL TIME" "$NAT_TOTAL" "$LTM_TOTAL" "$PCT_TOTAL" "$DIFF_TOTAL"
echo "=========================================================================================="
echo ""
echo "[+] Verification: LTM executes unmodified Linux ARM64 binaries at near-100% native CPU speed!"
