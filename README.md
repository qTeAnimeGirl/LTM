# LTM (Linux Translation Module)

LTM is a high-performance userspace translation layer that runs **unmodified Linux ARM64 (AArch64) ELF binaries** natively on **macOS Apple Silicon (Darwin ARM64 / M-series)** without CPU emulation or virtual machines.

Because Apple Silicon and 64-bit ARM Linux share the same AArch64 instruction set architecture, userland instructions execute at 100% native CPU speed. LTM handles the translation of Linux syscalls (`svc #0` → `brk #0` → Darwin Mach-O syscalls), memory page semantics (16KB Apple Silicon alignment), filesystem virtualization, and dynamic ELF linker loading (`PT_INTERP`).

---

## 🌟 Key Features

- **Native Execution (0% CPU Emulation Overhead)**: Runs 64-bit Linux ARM64 instructions directly on Apple Silicon cores.
- **Dynamic ELF Loader & PT_INTERP Support**: Automatically detects and loads dynamic interpreters (e.g. `/lib/ld-musl-aarch64.so.1`), initializing the Linux Aux Vector (`AT_PHDR`, `AT_BASE`, `AT_ENTRY`, `AT_RANDOM`, `AT_PAGESZ=16384`) conforming strictly to the Linux ARM64 ABI.
- **GNU Bash & BusyBox Native Shell Support**: Fully functional `/bin/bash` and `/bin/sh` with command execution, pipelines (`|`), I/O redirection (`>`, `<`), subshells, and process spawning (`fork`/`clone` + `execve`).
- **Native Fastfetch Rendering**: Automatically runs `fastfetch` with full distro ASCII logo, CPU topology, kernel version (`6.6.0-ltm`), memory telemetry, and terminal color palette.
- **Sandboxed Chroot Filesystem**: Strict containment jailed within a guest rootfs (`-r rootfs`), with virtual `/proc` (`cpuinfo`, `meminfo`, `version`, `cmdline`, `maps`, `exe`) and `/dev` devices. Zero host leaks or directory traversal escapes.
- **Darwin `_sigtramp` Bypass**: Custom assembly context restore engine (`ltm_resume_context`) enabling seamless child process spawning from within Darwin signal handlers without kernel `__sigreturn` panics.

---

## 🚀 Quick Start

### 1. Build LTM
```bash
make
```

### 2. Download Alpine Linux aarch64 Rootfs & Fastfetch / Bash
```bash
./scripts/setup_rootfs.sh
```

### 3. Run Fastfetch
```bash
./ltm /usr/bin/fastfetch
```

### 4. Run GNU Bash
```bash
# Run a one-line Bash script with pipes and subcommands
./ltm /bin/bash -c "echo 'Hello from GNU Bash on macOS!'; /usr/bin/fastfetch"

# Run an interactive Bash shell
./ltm /bin/bash
```

### 5. Run BusyBox Shell
```bash
# Run a shell pipeline
./ltm /bin/sh -c "ls -la / | grep bin"
```

---

## 🧪 Automated Test Suite

LTM includes an automated test harness validating the ELF loader, Linux ABI, file I/O, sandbox security containment, procfs emulation, and memory layout:

```bash
./run_tests.sh
```

All 6 test suites run with 100% pass rate:
1. `test_hello`: Freestanding assembly Linux ARM64 ELF execution.
2. `test_args_env`: Verification of argc, argv, envp, and auxiliary vectors.
3. `test_file_io`: Multi-operation sandboxed openat, write, lseek, read, close.
4. `test_sandbox_defense`: Penetration test verifying strict rejection of host path breakouts (`/Library`, `/Users`, `/System`, `../../`).
5. `test_procfs`: Virtual `/proc/version`, `/proc/cpuinfo`, and `/proc/self/cmdline`.
6. `test_memory`: Dynamic heap (`brk`) expansion and anonymous `mmap` allocations.

---

## 🏗️ Architecture

```
Linux ARM64 ELF (Static or Dynamic)
       │
       ▼
 [ ELF Loader ] ──> Mmaps segments (16KB aligned) + Loads PT_INTERP + Prepares Linux ABI Stack
       │
       ▼
 [ Instruction Patcher ] ──> Rewrites `svc #0` (0xd4000001) to `brk #0` (0xd4200000)
       │
       ▼
 [ Darwin SIGTRAP Handler ] ──> Intercepts `brk #0`
       │
       ├─► [ Syscall Dispatcher ] ──> Unpacks x8, x0-x5
       ├─► [ Filesystem Sandbox ] ──> Resolves path to container rootfs (blocks host escapes)
       ├─► [ Syscall Handlers ]   ──> Translates Linux ABI <-> macOS Darwin Mach/BSD APIs
       │
       ▼
 [ Guest Context Resume ] ──> Advances PC + 4, restores registers x0-x30, resumes execution
```

---

## 📄 License
MIT License.
