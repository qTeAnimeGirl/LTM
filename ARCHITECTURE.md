# LTM (Linux Translation Module) — Technical Architecture

This document provides an in-depth architectural and systems-level specification of **LTM**, an ultra-low-overhead userspace translation layer that executes unmodified Linux ARM64 (AArch64) ELF binaries natively on macOS Apple Silicon (Darwin ARM64 / M-series).

---

## 1. High-Level System Overview

```
                      +---------------------------------------+
                      |   Unmodified Linux ARM64 ELF Binary   |
                      |     (GNU Bash, Fastfetch, Musl/GLibc) |
                      +---------------------------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |               LTM Runtime             |
                      +---------------------------------------+
                      | 1. ELF64 Loader (16KB page alignment) |
                      | 2. PT_INTERP Dynamic Linker Loader    |
                      | 3. Auxiliary Vector & Stack Setup     |
                      | 4. JIT SVC-to-BRK Instruction Patcher |
                      | 5. Darwin SIGTRAP Interceptor         |
                      | 6. Custom Context Resume Engine       |
                      | 7. Syscall Translator & ABI Bridge    |
                      | 8. Sandboxed Path Resolution Engine   |
                      | 9. Live Mach / Sysctl Hardware Engine |
                      +---------------------------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |       macOS Darwin Kernel (XNU)       |
                      |       Apple Silicon M3 Hardware       |
                      +---------------------------------------+
```

### Core Design Philosophy
- **0% CPU Emulation Overhead**: Because Apple Silicon and 64-bit Linux ARM64 use the exact same AArch64 instruction set (same general-purpose registers `x0`-`x30`, vector/NEON registers `v0`-`v31`, and execution modes), user application code executes at 100% native CPU speed directly on hardware.
- **Pure Userspace Architecture**: LTM runs entirely as an unprivileged userspace Darwin Mach-O binary. It requires no custom kernel extensions (KEXTs), no hypervisor framework (`Hypervisor.framework`), no root privileges, and no virtualization containers.
- **Strict Jail Sandboxing**: Guest Linux binaries are securely confined within a designated guest root filesystem directory (`host_rootfs`). Host paths (`/Library`, `/System`, `/Users`, `/private`, `/etc`) are completely inaccessible, and path breakout attempts (`../`) are safely clamped and rejected.

---

## 2. Memory Architecture & Apple Silicon Compatibility

### 2.1 The 16KB Page Size Challenge
- **Linux ARM64 Default**: Most Linux ARM64 distributions (including Debian, Ubuntu, and Arch Linux) default to **4KB (4096-byte)** virtual memory page sizes, though some kernels support 16KB or 64KB.
- **macOS Apple Silicon**: macOS on Apple Silicon enforces a strict **16KB (16384-byte)** hardware page size. Calling `mmap()`, `mprotect()`, or `munmap()` with non-16KB-aligned addresses or sizes on macOS will fail with `EINVAL`.

### 2.2 Segment Page Alignment & Overlap Resolution
When loading Linux ELF program headers (`PT_LOAD`):
1. **Virtual Address Normalization**: Segment virtual addresses are rounded down to 16KB boundaries:
   $$\text{vaddr\_aligned} = \text{vaddr} \ \& \ \sim(0x4000 - 1)$$
2. **Size Normalization**: The mapping size is rounded up to include the start offset:
   $$\text{length\_aligned} = (\text{vaddr} + \text{memsz} - \text{vaddr\_aligned} + 0x3FFF) \ \& \ \sim(0x3FFF)$$
3. **W^X Page Protection Overlaps**: In standard Linux 4KB binaries, a read-only code segment (`PT_LOAD`, `PF_R | PF_X`) may end in the middle of a 16KB page, while the subsequent data segment (`PF_R | PF_W`) begins in that same 16KB page.
   - Apple Silicon enforces strict `W^X` (Write XOR Execute) memory protections.
   - LTM detects overlapping pages and handles them by mapping with compatible permissions and copying `.data`/`.bss` bytes using page-aligned boundaries.

### 2.3 Memory Layout
To prevent collisions between the host Mach-O loader, Darwin libraries (`libSystem.B.dylib`), and guest Linux ELFs, LTM organizes memory into distinct virtual address spaces:

| Virtual Address Range | Component | Description |
|:---|:---|:---|
| `0x000000000 - 0x100000000` | `PAGEZERO` | macOS Darwin 4GB zero-page protection (guards against NULL dereferences) |
| `0x100000000 - 0x200000000` | LTM Mach-O Binary | Code, data, and stack of the LTM runtime itself |
| `0x400000000 - 0x480000000` | Guest Main Binary | Mapped Linux ELF executable (ET_EXEC or ET_DYN base) |
| `0x480000000 - 0x500000000` | Guest Heap (`brk`) | Managed dynamically via `handle_brk` |
| `0x500000000 - 0x580000000` | `PT_INTERP` Dynamic Linker | Mapped `ld-musl-aarch64.so.1` or `ld-linux-aarch64.so.1` |
| `0x580000000 - 0x600000000` | Guest Dynamic Libraries | Mapped dynamically via guest `mmap` calls |
| `0x7000000000 - 0x7000800000` | Guest Initial Stack | 8MB anonymous stack initialized with the Linux ARM64 ABI |

---

## 3. Dynamic ELF Loader & Linux ABI Initialization

### 3.1 ELF64 Parsing & Verification
`elf_load_binary()` parses the ELF header and program headers:
1. Validates `e_ident`: `0x7f 'E' 'L' 'F'`, `ELFCLASS64`, `ELFDATA2LSB`, `EV_CURRENT`.
2. Validates `e_machine == EM_AARCH64` (`0xb7` / 183).
3. Evaluates `e_type`:
   - `ET_EXEC`: Uses segment virtual addresses directly (offsetting if below `0x400000000`).
   - `ET_DYN` (Position Independent Executable): Relocates the base to `0x400000000`.

### 3.2 Dynamic Linker Loading (`PT_INTERP`)
When an ELF contains a `PT_INTERP` header (e.g. `/lib/ld-musl-aarch64.so.1`):
1. LTM extracts the interpreter path string.
2. Resolves the path within the guest `host_rootfs` (e.g., `rootfs/lib/ld-musl-aarch64.so.1`).
3. Loads the interpreter into memory at base `0x500000000`.
4. Sets the execution entry point to the dynamic linker's entry point (`interp_base + interp_ehdr.e_entry`).
5. Passes the main binary's entry point and program headers to the dynamic linker via the **Auxiliary Vector**.

### 3.3 Linux ARM64 Stack Layout & Auxiliary Vector (Auxv)
Upon entry, the Linux ARM64 dynamic linker expects the initial stack pointer (`SP`) to adhere strictly to the Linux AArch64 ABI specification:

```
SP ->  [ argc              ] (uint64_t)
       [ argv[0]           ] (pointer to string)
       [ argv[1]           ] ...
       [ NULL              ]
       [ envp[0]           ] (pointer to string)
       [ envp[1]           ] ...
       [ NULL              ]
       [ AT_PHDR  | value  ] (Auxv: address of program headers)
       [ AT_PHENT | value  ] (Auxv: size of program header entry)
       [ AT_PHNUM | value  ] (Auxv: number of program headers)
       [ AT_PAGESZ| 16384  ] (Auxv: system page size = 16KB)
       [ AT_BASE  | value  ] (Auxv: interpreter base address)
       [ AT_FLAGS | 0      ]
       [ AT_ENTRY | value  ] (Auxv: entry point of main binary)
       [ AT_UID   | 1000   ]
       [ AT_EUID  | 1000   ]
       [ AT_GID   | 1000   ]
       [ AT_EGID  | 1000   ]
       [ AT_CLKTCK| 100    ]
       [ AT_RANDOM| addr   ] (Auxv: pointer to 16 random bytes)
       [ AT_NULL  | 0      ] (Auxv terminator)
       ---------------------
       [ 16 random bytes   ]
       [ string table      ] (stores argv and envp strings)
```
- LTM strictly maintains **16-byte stack pointer alignment** (`SP % 16 == 0`).

---

## 4. Syscall Interception & Execution Engine

### 4.1 The Syscall Trap Strategy
On Linux ARM64, userspace invokes system calls using the `svc #0` instruction (`0xd4000001`). On macOS Apple Silicon:
- Darwin reserves `svc #0x80` for BSD/Mach syscalls.
- Executing `svc #0` under Darwin causes an unhandled exception or illegal instruction trap.
- Furthermore, macOS Darwin enforces strict hardened runtime memory integrity.

### 4.2 JIT Instruction Patching
To intercept syscalls without emulation:
1. `patch_executable_segments()` scans all loaded executable segments (`PF_X`).
2. Every 32-bit `svc #0` opcode (`0xd4000001`) is rewritten to `brk #0` (`0xd4200000`).
3. To safely write into executable pages on Apple Silicon:
   ```c
   mprotect(page_start, page_len, PROT_READ | PROT_WRITE);
   *target_addr = 0xd4200000;
   mprotect(page_start, page_len, PROT_READ | PROT_EXEC);
   ```
4. **Instruction Cache Invalidation**: Following modification, LTM flushes CPU instruction caches using `__builtin___clear_cache()` to guarantee hardware execution pipeline consistency.

### 4.3 Signal Interception & Context Handling
1. LTM installs a `SIGTRAP` handler via `sigaction` with `SA_SIGINFO | SA_NODEFER | SA_ONSTACK`.
2. When guest code executes `brk #0`, the CPU triggers `EXC_BREAKPOINT`, delivered by Darwin as `SIGTRAP`.
3. In `ltm_sigtrap_handler(int sig, siginfo_t *info, void *ucontext)`:
   - Registers are extracted from `ucontext_t`:
     - `x8`: Linux syscall number
     - `x0` - `x5`: Syscall arguments 1 through 6
     - `pc`: Program counter pointing to the `brk #0` instruction
   - `syscall_dispatch()` executes the mapped translation handler.
   - Return value is written to `x0` (negative error code on failure).
   - Program counter is advanced: `pc += 4` (skipping the 4-byte `brk #0`).

### 4.4 Darwin `_sigtramp` Bypass (`ltm_resume_context`)
Under macOS Darwin, returning normally from a signal handler executes Darwin's internal `_sigtramp`, which calls the private Mach system call `__sigreturn`.
- **The Child Process Trap**: When a guest program calls `fork()` or `clone()` from inside a syscall handler, the child process inherits the signal frame. When the child process attempts to `__sigreturn`, the Darwin XNU kernel detects mismatched signal frame security cookies and terminates the process with `brk #1`.
- **The Solution**: LTM bypasses `_sigtramp` and `__sigreturn` completely using a custom assembly context restoration stub:
  ```assembly
  .global _ltm_resume_context
  _ltm_resume_context:
      // x0 points to Darwin __darwin_arm_thread_state64
      // Restores x1..x30, SP, and jumps to PC directly via x0
  ```
  This ensures that guest execution resumes smoothly in both parent and child processes with zero kernel signal frame panics.

---

## 5. Terminal, TTY, & Interactive Shell Subsystem

### 5.1 The Termios ABI Incompatibility
GNU Bash and Readline rely heavily on `ioctl(fd, TCGETS, ...)` and `ioctl(fd, TCSETS, ...)` for raw mode, echo control, and terminal formatting.

| Field | Linux ARM64 `struct termios` | macOS Darwin `struct termios` |
|:---|:---|:---|
| **Total Size** | **36 bytes** | **72 bytes** |
| `c_iflag` | `tcflag_t` (uint32_t, 4 bytes) | `tcflag_t` (unsigned long, 8 bytes) |
| `c_oflag` | `tcflag_t` (uint32_t, 4 bytes) | `tcflag_t` (unsigned long, 8 bytes) |
| `c_cflag` | `tcflag_t` (uint32_t, 4 bytes) | `tcflag_t` (unsigned long, 8 bytes) |
| `c_lflag` | `tcflag_t` (uint32_t, 4 bytes) | `tcflag_t` (unsigned long, 8 bytes) |
| `c_line` | `cc_t` (uint8_t, 1 byte) | *(not present)* |
| `c_cc` | `cc_t[19]` (19 bytes) | `cc_t[20]` (20 bytes) |
| `c_ispeed` | *(encoded in flags)* | `speed_t` (unsigned long, 8 bytes) |
| `c_ospeed` | *(encoded in flags)* | `speed_t` (unsigned long, 8 bytes) |

If a Darwin `tcgetattr()` writes directly into a guest Linux buffer, it causes a **36-byte stack overflow**, corrupting local variables and triggering silent crashes.

LTM defines an explicit `struct linux_termios` and performs bidirectional translation:
- `copy_darwin_to_linux_termios()`: Maps Darwin baud rates and control characters to Linux ABI.
- `copy_linux_to_darwin_termios()`: Translates guest settings back to Darwin termios before `tcsetattr()`.

### 5.2 Terminal Job Control & SIGTTOU Isolation
When shells configure terminal process groups via `ioctl(TIOCSPGRP, &pgrp)`:
- If called by a background process, the Darwin kernel sends `SIGTTOU`, suspending the shell.
- LTM temporarily masks or ignores `SIGTTOU` during `tcsetpgrp` invocations, allowing smooth job control transitions without terminal lockups.

### 5.3 Event Loop Multiplexing (`pselect6`)
GNU Readline 8.2+ uses Linux `pselect6` (syscall 72) for its non-blocking event loop.
- LTM unpacks the Linux `struct timespec` and `sigset_t`, converting them to Darwin's `pselect()` parameters.
- Returning proper polling semantics prevents Readline from treating timeouts as EOF, keeping interactive Bash sessions alive.

---

## 6. Live Hardware Telemetry Engine

To ensure utilities like `fastfetch`, `lscpu`, `top`, and `htop` display authentic hardware metrics, LTM bridges Darwin kernel diagnostics directly into virtual Linux `/proc` and `/sys` interfaces.

### 6.1 Processor & Topology Telemetry
- **CPU Brand & Model**: Fetched via `sysctlbyname("machdep.cpu.brand_string")` (e.g. `Apple M3`).
- **Core Count**: Fetched via `sysctlbyname("hw.ncpu")` (e.g. `8`).
- **ARM64 CPU Features**: Synthesizes standard Linux feature flags (`fp`, `asimd`, `aes`, `sha1`, `sha2`, `crc32`, `atomics`).
- **Sysfs Topology**: Virtualizes `/sys/devices/system/cpu/online`, `present`, and `possible` as `0-N`.

### 6.2 Real-Time Host Memory Telemetry
Rather than reporting static memory values, LTM queries the Mach kernel VM subsystem in real-time:
```c
mach_port_t host = mach_host_self();
vm_size_t page_size;
host_page_size(host, &page_size);

vm_statistics64_data_t vm_stat;
mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&vm_stat, &count);
```
- **MemTotal**: Derived from `sysctlbyname("hw.memsize")`.
- **MemFree**: Calculated from `vm_stat.free_count * page_size`.
- **MemAvailable**: Calculated from `(vm_stat.free_count + vm_stat.inactive_count) * page_size`.
- **Cached / Active / Inactive**: Calculated directly from live Mach VM page counters.
- **Swap Usage**: Queried via `sysctlbyname("vm.swapusage")`.

### 6.3 System Uptime & Load Averages
- **Uptime**: Calculated dynamically using `gettimeofday()` and `sysctlbyname("kern.boottime")`.
- **Load Averages**: Extracted directly from Darwin `sysctlbyname("vm.loadavg")`.
- **Host Product Name**: Queried from `sysctlbyname("hw.model")` (e.g. `Mac15,13`) and populated into `/sys/class/dmi/id/product_name`.

---

## 7. Filesystem Sandboxing & Chroot Containment

### 7.1 Path Resolution Pipeline
All guest path operations (`openat`, `stat`, `newfstatat`, `execve`, `chdir`) pass through `sandbox_resolve_path()`:

1. **Working Directory Tracking**: Handles `AT_FDCWD` relative paths against the guest's tracked current directory.
2. **Guest Chroot Prefixing**: Maps container paths (`/bin`, `/usr`, `/lib`, `/etc`) into the isolated rootfs:
   $$\text{host\_path} = \text{host\_rootfs} + \text{guest\_path}$$
3. **Symlink Traversal Engine**: Follows relative and absolute symlinks within `host_rootfs` up to a recursion depth of 32 hops, preventing symlink loops.
4. **Breakout Defense**: Canonicalizes paths and strictly rejects any path resolving outside `host_rootfs`. Attempts to access `/System`, `/Library`, `/Users`, or `/private` return `-ENOENT` or `-EACCES`.
5. **Virtual Mounts**: Intercepts `/proc`, `/sys`, and `/dev` to route to dynamic virtual memory handlers.

---

## 8. Syscall Mapping Reference

Below is a summary of core Linux ARM64 system calls implemented in LTM:

| Linux Syscall | AArch64 x8 | Handler Function | Darwin Mechanism |
|:---|:---|:---|:---|
| `read` | 63 | `handle_read` | `read()` |
| `write` | 64 | `handle_write` | `write()` |
| `openat` | 56 | `handle_openat` | Sandboxed path resolution + `open()` / `openat()` |
| `close` | 57 | `handle_close` | `close()` |
| `lseek` | 62 | `handle_lseek` | `lseek()` |
| `readv` / `writev` | 65 / 66 | `handle_readv` / `handle_writev` | `readv()` / `writev()` |
| `pselect6` | 72 | `handle_pselect6` | Darwin `pselect()` with timespec conversion |
| `newfstatat` | 79 | `handle_newfstatat` | `fstatat()` + `stat` to `linux_stat` translation |
| `fstat` | 80 | `handle_fstat` | `fstat()` + `stat` to `linux_stat` translation |
| `fstatfs` | 44 | `handle_fstatfs` | `fstatfs()` + `statfs` to `linux_statfs64` translation |
| `exit` / `exit_group` | 93 / 94 | `handle_exit` / `handle_exit_group`| `exit()` |
| `set_tid_address` | 96 | `handle_set_tid_address` | Registers clear_child_tid pointer |
| `futex` | 98 | `handle_futex` | Emulated `FUTEX_WAIT`, `FUTEX_WAKE` using pthread mutex/cond |
| `brk` | 214 | `handle_brk` | Dynamic page allocation via `mmap` above `0x480000000` |
| `mmap` | 222 | `handle_mmap` | 16KB alignment + `MAP_ANONYMOUS` (0x20) -> `MAP_ANON` (0x1000) |
| `munmap` | 215 | `handle_munmap` | 16KB-aligned `munmap()` |
| `mprotect` | 226 | `handle_mprotect` | 16KB-aligned `mprotect()` + W^X compliance |
| `clone` | 220 | `handle_clone` | `fork()` emulation for process spawning |
| `execve` | 221 | `handle_execve` | Re-invokes LTM wrapper under guest rootfs |
| `wait4` | 260 | `handle_wait4` | `wait4()` |
| `ioctl` | 29 | `handle_ioctl` | Bidirectional termios translation, window sizing, TTY control |
| `sched_getaffinity` | 123 | `handle_sched_getaffinity` | Synthesizes CPU mask based on `hw.ncpu` |
| `uname` | 160 | `handle_uname` | Returns Linux 6.6.0-ltm on aarch64 |
| `sysinfo` | 179 | `handle_sysinfo` | Real uptime and live Mach VM statistics |

---

## 9. Verification & Performance

- **Regression Test Suite**: Automated 6-part test suite validating ELF loading, Linux ABI stack structure, sandboxed I/O, penetration defense against directory escapes, virtual procfs, and dynamic memory allocations (`./run_tests.sh`).
- **Interactive Shell Benchmark**: Verified with GNU Bash 5.2 (Alpine aarch64) and BusyBox `sh` supporting interactive keystrokes, tab completion, pipelines, job control, and clean exits.
- **Hardware Telemetry Benchmark**: Verified with `fastfetch` displaying dynamic Apple M3 CPU cores, 16KB page-based memory usage, and system uptime.
