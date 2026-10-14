#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_DIR="$SCRIPT_DIR/tests/bin"
LTM="$SCRIPT_DIR/ltm"

# Colors
C_RESET="\033[0m"
C_BOLD="\033[1m"
C_RED="\033[31m"
C_GREEN="\033[32m"
C_YELLOW="\033[33m"
C_BLUE="\033[34m"
C_CYAN="\033[36m"

echo -e "${C_BOLD}${C_BLUE}======================================================${C_RESET}"
echo -e "${C_BOLD}${C_BLUE}        LTM Automated Test Suite Execution           ${C_RESET}"
echo -e "${C_BOLD}${C_BLUE}   Linux ARM64 ELF on Apple Silicon Darwin Translator  ${C_RESET}"
echo -e "${C_BOLD}${C_BLUE}======================================================${C_RESET}\n"

# 1. Ensure test binaries are built
if [ ! -d "$BIN_DIR" ] || [ -z "$(ls -A "$BIN_DIR" 2>/dev/null)" ]; then
    echo -e "${C_YELLOW}[!] Test binaries not found. Building now...${C_RESET}"
    "$SCRIPT_DIR/tests/build_tests.sh"
fi

# 2. Check if ltm translation runtime is available
if [ ! -x "$LTM" ]; then
    echo -e "${C_YELLOW}[!] ltm translation binary not found at $LTM${C_RESET}"
    if [ -f "$SCRIPT_DIR/Makefile" ]; then
        echo -e "${C_CYAN}[+] Makefile detected. Attempting to build ltm...${C_RESET}"
        if make -C "$SCRIPT_DIR" ltm; then
            echo -e "${C_GREEN}[+] ltm binary successfully built.${C_RESET}"
        else
            echo -e "${C_RED}[-] Failed to build ltm via make.${C_RESET}"
            exit 2
        fi
    else
        echo -e "${C_YELLOW}[*] Waiting for Core Coder to produce the 'ltm' binary.${C_RESET}"
        echo -e "${C_YELLOW}[*] All 6 test ELF binaries are compiled and ready in $BIN_DIR:${C_RESET}"
        ls -la "$BIN_DIR"
        exit 2
    fi
fi

TOTAL=0
PASSED=0
FAILED=0

run_single_test() {
    local test_name="$1"
    local bin_path="$BIN_DIR/$test_name"
    local expected_pattern="$2"
    shift 2
    local extra_args=("$@")

    TOTAL=$((TOTAL + 1))
    echo -e "${C_BOLD}${C_CYAN}------------------------------------------------------${C_RESET}"
    echo -e "${C_BOLD}Test #$TOTAL: ${C_YELLOW}$test_name${C_RESET}"
    echo -e "Binary: $bin_path"
    if [ ${#extra_args[@]} -gt 0 ]; then
        echo -e "Arguments: ${extra_args[*]}"
    fi

    if [ ! -x "$bin_path" ]; then
        echo -e "${C_RED}[FAIL] Binary not executable or missing: $bin_path${C_RESET}"
        FAILED=$((FAILED + 1))
        return
    fi

    local stdout_file
    stdout_file="$(mktemp /tmp/ltm_test_out.XXXXXX)"
    local stderr_file
    stderr_file="$(mktemp /tmp/ltm_test_err.XXXXXX)"

    local start_time
    start_time="$(date +%s)"

    # Run under ltm translator
    set +e
    if [ ${#extra_args[@]} -gt 0 ]; then
        "$LTM" "$bin_path" "${extra_args[@]}" >"$stdout_file" 2>"$stderr_file"
    else
        "$LTM" "$bin_path" >"$stdout_file" 2>"$stderr_file"
    fi
    local exit_code=$?
    set -u

    local end_time
    end_time="$(date +%s)"
    local duration=$((end_time - start_time))

    local stdout_content
    stdout_content="$(cat "$stdout_file")"
    local stderr_content
    stderr_file_content="$(cat "$stderr_file")"

    rm -f "$stdout_file" "$stderr_file"

    # Analyze outcome
    local passed=1
    local fail_reason=""

    if [ $exit_code -ne 0 ]; then
        passed=0
        fail_reason="Non-zero exit code: $exit_code"
        if [ $exit_code -eq 139 ]; then
            fail_reason="Segmentation fault (SIGSEGV - 11)"
        elif [ $exit_code -eq 134 ]; then
            fail_reason="Aborted (SIGABRT - 6)"
        elif [ $exit_code -eq 133 ]; then
            fail_reason="Trace/BPT trap (SIGTRAP - 5)"
        elif [ $exit_code -eq 140 ]; then
            fail_reason="Bad system call (SIGSYS - 12)"
        fi
    fi

    if [ -n "$expected_pattern" ]; then
        if ! echo "$stdout_content" | grep -q "$expected_pattern"; then
            passed=0
            if [ -z "$fail_reason" ]; then
                fail_reason="Missing expected pattern: '$expected_pattern'"
            else
                fail_reason="$fail_reason; Missing pattern: '$expected_pattern'"
            fi
        fi
    fi

    if [ $passed -eq 1 ]; then
        echo -e "${C_GREEN}${C_BOLD}[PASS]${C_RESET} ${C_GREEN}$test_name passed cleanly (${duration}s)${C_RESET}"
        if [ -n "$stdout_content" ]; then
            echo -e "Output preview:\n$stdout_content" | head -n 10
        fi
        PASSED=$((PASSED + 1))
    else
        echo -e "${C_RED}${C_BOLD}[FAIL]${C_RESET} ${C_RED}$test_name failed: $fail_reason${C_RESET}"
        if [ -n "$stdout_content" ]; then
            echo -e "${C_BOLD}STDOUT:${C_RESET}\n$stdout_content"
        fi
        if [ -n "$stderr_file_content" ]; then
            echo -e "${C_BOLD}STDERR:${C_RESET}\n$stderr_file_content"
        fi
        FAILED=$((FAILED + 1))
    fi
}

# Run core and sprint tests
run_single_test "test_hello" "Hello from unmodified Linux ARM64 ELF!"
run_single_test "test_args_env" "All Linux ABI stack checks passed!" "arg1" "arg2" "var=123"
run_single_test "test_file_io" "All file operations"
run_single_test "test_sandbox_defense" "All host escape vectors strictly prevented!"
run_single_test "test_procfs" "Virtual procfs successfully inspected"
run_single_test "test_memory" "Both brk and mmap memory allocation validated!"
run_single_test "test_ps_procfs" "test_ps_procfs: All /proc directory entries and process files verified!"
run_single_test "test_wave1_syscalls" "All 14 Wave 1 syscalls verified across all 3 Mandate cases!"

# Test BusyBox ps if busybox is available
if [ -x "$SCRIPT_DIR/rootfs/bin/busybox" ]; then
    TOTAL=$((TOTAL + 1))
    echo -e "${C_BOLD}${C_CYAN}------------------------------------------------------${C_RESET}"
    echo -e "${C_BOLD}Test #$TOTAL: ${C_YELLOW}busybox ps / ps aux${C_RESET}"
    set +e
    PS_OUT="$("$LTM" /bin/busybox ps)"
    PS_EXIT=$?
    set -u
    if [ $PS_EXIT -eq 0 ] && echo "$PS_OUT" | grep -q -E "PID.*USER.*COMMAND" && echo "$PS_OUT" | grep -q -E "[0-9]+.*"; then
        echo -e "${C_GREEN}${C_BOLD}[PASS]${C_RESET} ${C_GREEN}busybox ps displayed processes:${C_RESET}\n$PS_OUT"
        PASSED=$((PASSED + 1))
    else
        echo -e "${C_RED}${C_BOLD}[FAIL]${C_RESET} ${C_RED}busybox ps failed to display running processes!${C_RESET}"
        echo -e "Output:\n$PS_OUT"
        FAILED=$((FAILED + 1))
    fi
fi

# Run Wave 2 syscall test suite as Test #10
run_single_test "test_wave2_syscalls" "All Wave 2 syscalls verified across all 3 Mandate cases!"

# Run standalone syscall test binaries (Sprint 10)
if [ -d "$BIN_DIR/syscalls" ]; then
    for sys_bin in "$BIN_DIR/syscalls"/test_sys_*; do
        if [ -x "$sys_bin" ]; then
            bin_name="syscalls/$(basename "$sys_bin")"
            run_single_test "$bin_name" "passed!"
        fi
    done
fi

# Clean up any sandbox temporary files created during testing
rm -f "$SCRIPT_DIR/ltm_io_test_file.tmp" "$SCRIPT_DIR/sandbox_test.txt" "$SCRIPT_DIR/wave1_test_file.tmp" "$SCRIPT_DIR/wave2_test_file.tmp" "$SCRIPT_DIR/wave2_copy_file.tmp" "$SCRIPT_DIR"/tmp_sys_*.tmp

# Summary
echo -e "\n${C_BOLD}${C_BLUE}======================================================${C_RESET}"
echo -e "${C_BOLD}                   TEST SUITE SUMMARY                 ${C_RESET}"
echo -e "${C_BOLD}${C_BLUE}======================================================${C_RESET}"
echo -e "Total Tests : $TOTAL"
echo -e "Passed      : ${C_GREEN}${C_BOLD}$PASSED${C_RESET}"
echo -e "Failed      : ${C_RED}${C_BOLD}$FAILED${C_RESET}"

if [ $FAILED -eq 0 ]; then
    echo -e "${C_GREEN}${C_BOLD}ALL TESTS PASSED! Translation module validated.${C_RESET}\n"
    exit 0
else
    echo -e "${C_RED}${C_BOLD}SOME TESTS FAILED! Inspect diagnostic output above.${C_RESET}\n"
    exit 1
fi
