# LTM (Linux Translation Module)

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-macOS%20Apple%20Silicon%20(M--series)-lightgrey.svg)]()
[![Target](https://img.shields.io/badge/Guest-Linux%20ARM64%20(AArch64)-orange.svg)]()
[![Tests](https://img.shields.io/badge/Tests-6%2F6%20Passing-brightgreen.svg)]()

LTM is a high-performance userspace translation layer that executes **unmodified Linux ARM64 (AArch64) ELF binaries natively on macOS Apple Silicon** (Darwin ARM64 / M1, M2, M3, M4) without CPU emulation or virtual machines.

Because Apple Silicon and 64-bit Linux ARM64 share the exact same AArch64 instruction set architecture, userland instructions run at **100% native CPU speed with 0% emulation overhead**. LTM bridges the gap by translating Linux syscalls (`svc #0` → `brk #0` → Darwin Mach/BSD APIs), handling Apple Silicon's 16KB memory page semantics, virtualizing `/proc` and `/sys` using live host hardware telemetry, and dynamically loading Linux ELF dynamic linkers (`PT_INTERP`).

---

## 🌟 Key Features

- **🚀 100% Native Speed (0% CPU Emulation)**: Runs 64-bit Linux ARM64 instructions directly on Apple Silicon cores with zero translation or emulation penalty.
- **⚡ Dynamic ELF Linker & PT_INTERP Support**: Automatically detects and loads dynamic interpreters (`/lib/ld-musl-aarch64.so.1`), initializing the Linux Aux Vector (`AT_PHDR`, `AT_BASE`, `AT_ENTRY`, `AT_RANDOM`, `AT_PAGESZ=16384`) strictly conforming to the Linux ARM64 ABI.
- **🌐 Full Internet & Network Connectivity**: TCP, UDP, and Unix domain sockets, complete DNS resolution, `setsockopt`/`getsockopt` translation, `sendmsg`/`recvmsg` with ABI conversion, and epoll event loops (`epoll_create1`, `epoll_ctl`, `epoll_pwait`). Supports `wget`, `curl`, and HTTPS TLS 1.3.
- **📂 Virtual Directory Browsing (`/proc`, `/sys`, `/dev`)**: Full directory navigation and listing (`cd /proc && ls -la && cat cpuinfo`) with live hardware telemetry, kernel version, uptime, load averages, memory info, and synthetic entries.
- **🔌 Complete Host Device Mapping (`/dev`)**: Standard character devices (`/dev/null`, `/dev/zero`, `/dev/urandom`, `/dev/random`, `/dev/tty`, `/dev/console`, `/dev/ptmx`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr`, `/dev/fd/*`).
- **🖥️ Full Interactive GNU Bash & BusyBox Shells**: Run GNU Bash 5.2 and BusyBox `/bin/sh` interactively with command history, tab completion, pipelines (`|`), I/O redirection (`>`, `<`), and job control.
- **📊 Native Fastfetch with Live Apple Silicon Telemetry**: Runs `fastfetch` with Alpine Linux ASCII art, displaying real host Apple Silicon CPU brand (`Apple M3`), actual core count (8 cores), real-time Mach VM memory statistics, uptime, and load averages.
- **⏱️ CPU Power Benchmark Suite**: Built-in side-by-side benchmark (`./scripts/run_benchmark.sh`) comparing native macOS Darwin ARM64 performance with LTM Linux ARM64 ELF execution across 5 compute-intensive workloads.
- **🛡️ Sandboxed Chroot Jail**: Strict containment within a designated rootfs directory. Directory traversal attempts (`../`) and host paths (`/Library`, `/System`, `/Users`, `/private`) are strictly blocked.
- **📦 Built-in Rootfs Guard & Auto-Installer**: If no root filesystem is detected, LTM safely refuses execution and offers an automatic one-click installer (`./ltm --install-rootfs`) that downloads and configures Alpine Linux aarch64, Fastfetch, and GNU Bash.
- **🔄 Darwin `_sigtramp` Bypass**: Custom assembly context restoration engine (`_ltm_resume_context`) enabling seamless child process spawning (`fork`/`clone` + `execve`) from inside signal handlers without kernel signal frame panics.
- **⌨️ Bidirectional Linux 36-Byte <-> Darwin 72-Byte Termios Translation**: Seamlessly bridges Linux terminal ioctl structures with Darwin termios, preventing stack corruption and enabling proper terminal formatting.

---

## 🚀 Quick Start

### 1. Build LTM
```bash
make
```

### 2. Install Rootfs & Tools (One-Step Installer)
LTM includes a built-in installer that downloads Alpine Linux aarch64, Fastfetch, and GNU Bash directly:
```bash
./ltm --install-rootfs
```
*(Alternatively, you can run `./scripts/setup_rootfs.sh`)*

### 3. Run CPU Power Benchmark (Native vs LTM)
Compare macOS Apple Silicon native execution speed directly against LTM Linux ELF execution:
```bash
./scripts/run_benchmark.sh
```

### 4. Test Internet Connectivity
```bash
./ltm /usr/bin/wget -O - http://example.com
```

### 5. Run Fastfetch
```bash
./ltm /usr/bin/fastfetch
```

### 6. Interactive GNU Bash Shell
```bash
# Launch an interactive GNU Bash session
./ltm /bin/bash

# Or browse /proc inside Bash
./ltm /bin/bash -c "cd /proc && ls -la && cat cpuinfo"
```

### 7. Interactive BusyBox Shell
```bash
./ltm /bin/sh
```

---

## 🛠️ CLI Usage & Options

```
Usage: ./ltm [options] <elf-binary> [args...]

Options:
  -r <path>             Specify guest rootfs directory (default: ./rootfs)
  -v, --verbose         Enable verbose debugging output (also via LTM_DEBUG=1)
  -s, --strace          Enable syscall tracing (also via LTM_STRACE=1)
  -i, --install-rootfs  Download and configure Alpine Linux aarch64 rootfs + Fastfetch + Bash
  -h, --help            Show help and usage information
```

---

## 🧪 Automated Test Suite

LTM includes a comprehensive automated test harness validating the ELF loader, Linux ABI, file I/O, sandbox security containment, procfs emulation, and dynamic memory layout:

```bash
./run_tests.sh
```

### Test Suite Coverage:
| Test Name | Description | Status |
|:---|:---|:---:|
| `test_hello` | Freestanding assembly Linux ARM64 ELF execution (`write`, `exit`) | **PASS** |
| `test_args_env` | Verification of argc, argv, envp, and auxiliary vectors (`AT_*`) | **PASS** |
| `test_file_io` | Sandboxed file lifecycle (`openat`, `write`, `lseek`, `read`, `close`) | **PASS** |
| `test_sandbox_defense` | Host-escape penetration test rejecting `/Library`, `/Users`, `/System`, `../../` | **PASS** |
| `test_procfs` | Virtual `/proc/version`, `/proc/cpuinfo`, and `/proc/self/cmdline` | **PASS** |
| `test_memory` | Dynamic heap (`brk`) expansion and anonymous `mmap` allocations | **PASS** |

---

## 🏗️ Technical Architecture

For complete systems specifications, memory layout diagrams, and ABI translation details, please see **[ARCHITECTURE.md](file:///Users/qteanimegirl/Documents/LTM/ARCHITECTURE.md)**.

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
       ├─► [ Live Hardware Engine ] ──> Mach VM stats + sysctl (Apple M3, 8 cores, live RAM)
       ├─► [ Syscall Handlers ]   ──> Translates Linux ABI <-> macOS Darwin Mach/BSD APIs
       │
       ▼
 [ Context Resume Engine ] ──> Custom assembly stub restores x0-x30 and resumes guest execution
```

---

## 🤖 For AI Agents & Contributors

If you are an autonomous AI coding agent or a human developer extending or forking LTM, consult **[AGENTS.md](file:///Users/qteanimegirl/Documents/LTM/AGENTS.md)** for:
- The **Five Golden Architectural Invariants** (16KB page alignment, 36-byte termios, sandbox integrity, live telemetry, signal bypass).
- Step-by-step instructions for adding new Linux syscalls.
- Debugging playbooks and the 7-Tier capability roadmap.

---

## 📄 License
MIT License.
