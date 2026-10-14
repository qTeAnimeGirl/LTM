# AI Agent & Contributor Handbook for LTM

Welcome, Agent! 🤖

If you are an autonomous AI coding agent (e.g. Antigravity, Claude, Copilot, SWE-bench) or a developer who has forked or is extending the **LTM (Linux Translation Module)** repository, this document is your definitive operating manual.

---

## 1. Project Mission & North Star

**Mission**: Enable 100% native userspace execution of unmodified Linux ARM64 (AArch64) ELF binaries on Apple Silicon macOS (Darwin ARM64 / M-series) without CPU emulation or virtual machines.

**North Star Milestones**:
- [x] **Milestone 1**: Freestanding & static Linux ARM64 ELF execution.
- [x] **Milestone 2**: Dynamic ELF loading (`PT_INTERP`), Musl C runtime, and BusyBox `/bin/sh`.
- [x] **Milestone 3**: Full GNU Bash 5.2 interactive shell and native `fastfetch` with live hardware telemetry.
- [ ] **Milestone 4**: Linux socket networking, epoll event loops, and package manager support (`apk add`).
- [ ] **Milestone 5**: Full Glibc compatibility, TLS, multi-threaded `clone()` / `pthreads`, and modern Linux concurrency.

---

## 2. Five Golden Architectural Invariants

Whenever you modify LTM, you **MUST** uphold these five invariants:

### Rule 1: The 16KB Memory Page Rule
macOS Apple Silicon hardware strictly enforces **16KB (0x4000) page sizes**. Linux binaries commonly assume 4KB pages.
- Any memory address or size passed to Darwin `mmap`, `mprotect`, or `munmap` **must be aligned to 16KB boundaries** (`0x4000`).
- Round start addresses *down* to the nearest 16KB: `addr & ~0x3FFF`.
- Round segment sizes *up* to cover the page span: `((addr + len - aligned_addr) + 0x3FFF) & ~0x3FFF`.
- Failure to follow this rule will cause Darwin to return `EINVAL` on syscalls or crash with `EXC_BAD_ACCESS`.

### Rule 2: The 36-Byte Termios Rule
- Linux ARM64 `struct termios` is **36 bytes** (with 32-bit `tcflag_t`).
- macOS Darwin `struct termios` is **72 bytes** (with 64-bit `unsigned long` flags).
- **NEVER** pass a guest pointer directly to Darwin `tcgetattr()` or `tcsetattr()`. Doing so causes a **36-byte stack overflow** that corrupts the guest stack.
- Always use the translation functions: `copy_darwin_to_linux_termios()` and `copy_linux_to_darwin_termios()` defined in `include/syscalls.h`.

### Rule 3: Zero Sandbox Breakouts
- Linux binaries must never have unmediated access to the host macOS filesystem.
- All guest file paths must be resolved via `sandbox_resolve_path()`.
- Host system directories (`/Library`, `/System`, `/Users`, `/private`, `/etc`) are forbidden. Breakout attempts must return `-ENOENT` (-2) or `-EACCES` (-13).
- Always verify path canonicalization against `g_ltm.host_rootfs`.

### Rule 4: No Hardcoded Hardware Simulation
- Hardware information exposed via `/proc` or `/sys` must reflect the actual host hardware.
- Use `sysctl` (`machdep.cpu.brand_string`, `hw.ncpu`, `hw.memsize`, `hw.model`, `kern.boottime`, `vm.loadavg`) and Mach VM kernel APIs (`host_statistics64(HOST_VM_INFO64)`).
- Never reintroduce hardcoded dummy strings or fake RAM capacities.

### Rule 5: Keep the Signal Frame Clean (`_ltm_resume_context`)
- Never rely on Darwin's `_sigtramp` / `__sigreturn` to resume guest context after `fork()` or `clone()`. Darwin XNU will panic with `brk #1` due to signal cookie mismatch.
- Always use the custom assembly restoration engine: `_ltm_resume_context` (`src/jump_stub.s`).

---

## 3. Codebase Map

| Path | Purpose |
|:---|:---|
| `include/ltm.h` | Core runtime definitions, global state (`g_ltm`), logging macros, Linux ARM64 syscall numbers. |
| `include/elf_loader.h` | Declarations for ELF64 parser, segment loader, and stack initialization. |
| `include/patcher.h` | Instruction patcher (`svc #0` -> `brk #0`) and cache invalidation. |
| `include/sandbox.h` | Sandboxed path resolution, chroot isolation, and virtual `/proc` & `/sys` engines. |
| `include/syscalls.h` | Syscall dispatcher prototypes, Linux struct definitions (`linux_stat`, `linux_termios`, `linux_statfs64`). |
| `src/main.c` | CLI entry point, flag parsing (`-v`, `-s`, `-r`, `-i`), rootfs guard, and auto-installer. |
| `src/elf_loader.c` | ELF parsing, `PT_LOAD` mapping, `PT_INTERP` dynamic linker resolution, and Auxv generation. |
| `src/patcher.c` | JIT scanner rewriting `0xd4000001` to `0xd4200000` with Apple Silicon W^X page handling. |
| `src/sandbox.c` | Chroot path resolution, symlink following, and live host telemetry generation. |
| `src/syscall_dispatcher.c`| `SIGTRAP` signal handler, register extraction (`x8`, `x0`-`x5`), dispatch table, and `_ltm_resume_context` call. |
| `src/syscall_handlers.c` | Handlers translating Linux syscall semantics to Darwin Mach/BSD APIs. |
| `src/jump_stub.s` | AArch64 assembly stubs: `jump_to_elf` (sets SP and jumps) and `_ltm_resume_context` (signal bypass). |
| `scripts/setup_rootfs.sh` | Shell script to download and unpack Alpine Linux aarch64 minirootfs, Fastfetch, and GNU Bash. |
| `tests/` | Source code for automated Linux test binaries (`test_hello`, `test_args_env`, `test_file_io`, etc.). |
| `run_tests.sh` | Automated regression test harness verifying 6 core subsystems. |
| `development_log.md` | Persistent chronological log of all architectural decisions and sprint outcomes. |

---

## 4. How to Add a New Linux Syscall (Step-by-Step)

When implementing a new Linux system call:

### Step 1: Check the Syscall Number
Verify the ARM64 syscall number in `include/ltm.h`. For example:
```c
#define LINUX_SYS_socket        198
#define LINUX_SYS_bind          200
#define LINUX_SYS_connect       203
```

### Step 2: Declare the Handler
Add the handler prototype in `include/syscalls.h`:
```c
int64_t handle_socket(int64_t domain, int64_t type, int64_t protocol);
```

### Step 3: Dispatch in `src/syscall_dispatcher.c`
Add the case in `syscall_dispatch()`:
```c
case LINUX_SYS_socket:
    return handle_socket(arg0, arg1, arg2);
```

### Step 4: Implement in `src/syscall_handlers.c`
Translate arguments, invoke Darwin BSD API, and map errors to negative Linux errno:
```c
int64_t handle_socket(int64_t domain, int64_t type, int64_t protocol) {
    int ret = socket((int)domain, (int)type, (int)protocol);
    if (ret < 0) {
        return darwin_to_linux_errno(errno);
    }
    return ret;
}
```

### Step 5: Test and Verify
1. Build LTM: `make`
2. Run regression tests: `./run_tests.sh`
3. Run the live binary that exercises the syscall (e.g. `./ltm /bin/bash`).

---

## 5. Debugging & Troubleshooting

### Enable Verbose Logging
Run with `-v` or set `LTM_DEBUG=1`:
```bash
./ltm -v /usr/bin/fastfetch
```

### Enable Syscall Tracing (strace mode)
Run with `-s` or set `LTM_STRACE=1` to print every intercepted syscall, arguments, and return codes:
```bash
./ltm -s /bin/bash -c "echo hi"
```

### Diagnosing Crashes (SIGSEGV / SIGBUS / EXC_BAD_ACCESS)
- Inspect the fault address: is it near `0x0`? (Null pointer dereference).
- Is it around `0x7000...`? (Stack pointer misalignment or overflow).
- Is it an `EINVAL` from `mmap` or `mprotect`? (Check 16KB page alignment: `addr % 0x4000 != 0`).
- Is it an unrecognized syscall? Look for `[LTM:WARN] Unhandled syscall #...` in stderr.

---

## 6. Continuous Integration & Swarm Roles

When running as an autonomous multi-agent swarm:

1. **Planner Agent (Overseer)**:
   - Maintains the roadmap and backlog in `development_log.md`.
   - Identifies blockers and breaks complex features into small implementation sprints.
2. **Coder Agent (Core Systems)**:
   - Implements C and assembly code adhering to the 5 Invariants.
   - Ensures code builds with `-Wall -Wextra` with zero warnings.
3. **Tester Agent (QA & Validation)**:
   - Builds test binaries and executes `./run_tests.sh`.
   - Validates live applications (`fastfetch`, `bash`, `sh`).
   - Reports reproduction steps and register dumps upon failure.

---

## 7. Future Capability Roadmap

Agents should implement upcoming capabilities in the following sequence:

- **Tier 4.1: Networking Fundamentals**: `socket` (198), `bind` (200), `listen` (201), `accept` (202), `connect` (203), `getsockname` (204), `getpeername` (205), `sendto` (206), `recvfrom` (207), `setsockopt` (208), `getsockopt` (209).
- **Tier 4.2: High-Performance Event Polling**: `epoll_create1` (20), `epoll_ctl` (21), `epoll_pwait` (22) mapped to Darwin `kqueue` / `kevent`.
- **Tier 4.3: Inter-Process Communication**: `eventfd2` (19), `signalfd4` (74), `timerfd_create` (85), `timerfd_settime` (86).
- **Tier 5.1: Package Management**: Enable Alpine `apk add` inside `rootfs` with network socket connectivity.
- **Tier 5.2: Multi-Threading (`pthreads`)**: Enhanced `clone` (`CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_THREAD | CLONE_SIGHAND`) mapped to Darwin `pthread_create` with private TLS blocks.
