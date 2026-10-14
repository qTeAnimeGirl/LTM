# LTM (Linux To Mac) — Technical Architecture

This document provides an in-depth architectural and systems-level specification of **LTM (Linux To Mac)**, an ultra-low-overhead userspace translation layer that executes unmodified Linux ARM64 (AArch64) ELF binaries natively on macOS Apple Silicon (Darwin ARM64 / M1, M2, M3, M4).

---

## 1. High-Level System Overview

```text
                      +---------------------------------------+
                      |   Unmodified Linux ARM64 ELF Binary   |
                      |     (GNU Bash, Fastfetch, Musl/GLibc) |
                      +---------------------------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |          LTM (Linux To Mac)           |
                      +---------------------------------------+
                      | 1. ELF64 Loader (16KB page alignment) |
                      | 2. PT_INTERP Dynamic Linker Loader    |
                      | 3. Auxiliary Vector & Stack Setup     |
                      | 4. JIT SVC-to-BRK Instruction Patcher |
                      | 5. Darwin SIGTRAP Interceptor         |
                      | 6. Custom Context Resume Engine       |
                      | 7. Real-Time Signal Delivery Engine   |
                      | 8. Syscall Translator & ABI Bridge    |
                      | 9. Sandboxed Path Resolution Engine   |
                      | 10. Live Mach / Sysctl Hardware Engine|
                      | 11. ICMP Decapsulation & Socket Bridge|
                      +---------------------------------------+
                                          |
                                          v
                      +---------------------------------------+
                      |       macOS Darwin Kernel (XNU)       |
                      |       Apple Silicon M3 Hardware       |
                      +---------------------------------------+
```

### Core Design Philosophy
- **0% CPU Emulation Overhead**: Because Apple Silicon and 64-bit Linux ARM64 use the exact same AArch64 instruction set (identical general-purpose registers `x0`-`x30`, vector/NEON registers `v0`-`v31`, and execution modes), user application code executes at 100% native CPU speed directly on hardware.
- **Pure Userspace Architecture**: LTM runs entirely as an unprivileged userspace Darwin Mach-O binary. It requires no custom kernel extensions (KEXTs), no hypervisor framework (`Hypervisor.framework`), no root privileges, and no virtualization containers.
- **Strict Jail Sandboxing**: Guest Linux binaries are securely confined within a designated guest root filesystem directory (`host_rootfs`). Host paths (`/Library`, `/System`, `/Users`, `/private`, `/etc`) are completely inaccessible, and path breakout attempts (`../`) are safely clamped and rejected.
- **Kernel Personality**: Identified to guest userspace as `LTM 2026.1A` across `uname(2)`, `/proc/version`, and `/proc/sys/kernel/osrelease`.

---

## 2. Memory Architecture & Apple Silicon Compatibility

### 2.1 The 16KB Page Size Invariant
- **Linux ARM64 Default**: Most standard Linux ARM64 distributions default to **4KB (4096-byte)** virtual memory page sizes.
- **macOS Apple Silicon**: macOS on Apple Silicon hardware strictly enforces a **16KB (16384-byte / 0x4000)** hardware page size. Calling `mmap()`, `mprotect()`, or `munmap()` with non-16KB-aligned addresses or sizes on macOS will immediately fail with `EINVAL` or trigger `EXC_BAD_ACCESS`.

### 2.2 Segment Page Alignment & Overlap Resolution
When loading Linux ELF program headers (`PT_LOAD`):
1. **Virtual Address Normalization**: Segment virtual addresses are rounded down to 16KB boundaries:
   $$\text{vaddr\_aligned} = \text{vaddr} \ \& \ \sim(0x4000 - 1)$$
2. **Size Normalization**: The mapping size is rounded up to cover the full page span:
   $$\text{length\_aligned} = (\text{vaddr} + \text{memsz} - \text{vaddr\_aligned} + 0x3FFF) \ \& \ \sim(0x3FFF)$$
3. **W^X Page Protection Overlaps**: In standard Linux 4KB binaries, a read-only code segment (`PT_LOAD`, `PF_R | PF_X`) may end in the middle of a 16KB page, while the subsequent data segment (`PF_R | PF_W`) begins in that same 16KB page.
   - Apple Silicon enforces strict `W^X` (Write XOR Execute) memory protections.
   - LTM detects overlapping pages and handles them by mapping with compatible permissions and copying `.data`/`.bss` bytes using page-aligned boundaries.

### 2.3 Virtual Address Space Organization
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

```text
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
       [ AT_UID   | 0      ] (Auxv: root UID)
       [ AT_EUID  | 0      ] (Auxv: root EUID)
       [ AT_GID   | 0      ] (Auxv: root GID)
       [ AT_EGID  | 0      ] (Auxv: root EGID)
       [ AT_CLKTCK| 100    ]
       [ AT_RANDOM| addr   ] (Auxv: pointer to 16 random bytes)
       [ AT_NULL  | 0      ] (Auxv terminator)
       ---------------------
       [ 16 random bytes   ]
       [ string table      ] (stores argv and envp strings)
```
- LTM strictly maintains **16-byte stack pointer alignment** (`SP % 16 == 0`).

---

## 4. Syscall Interception & Signal Architecture

### 4.1 JIT Instruction Patching (`svc #0` -> `brk #0`)
On Linux ARM64, userspace invokes system calls using the `svc #0` opcode (`0xd4000001`). Under macOS Apple Silicon, Darwin reserves `svc #0x80` for Mach/BSD syscalls, and raw `svc #0` triggers unhandled kernel traps.
1. `patch_executable_segments()` scans all executable memory ranges (`PF_X`).
2. Every 32-bit `svc #0` opcode (`0xd4000001`) is hot-patched to `brk #0` (`0xd4200000`).
3. Memory protection is temporarily promoted to `PROT_READ | PROT_WRITE`, patched, restored to `PROT_READ | PROT_EXEC`, and flushed using `__builtin___clear_cache()` and `sys_icache_invalidate()`.

### 4.2 Trap Interception & Dispatch
1. LTM registers an alternate 1MB signal stack using `sigaltstack()` and hooks `SIGTRAP` with `SA_SIGINFO | SA_ONSTACK | SA_NODEFER`.
2. When guest code executes `brk #0`, Darwin delivers `SIGTRAP`.
3. In `ltm_sigtrap_handler()`:
   - Registers are extracted from `ucontext_t`: `x8` (syscall number), `x0..x5` (arguments), `pc`.
   - `syscall_dispatch()` executes the translation handler.
   - Return value is written to `x0`.
   - Program counter is advanced: `pc += 4` (skipping the 4-byte `brk #0`).

### 4.3 Darwin `_sigtramp` Bypass (`ltm_resume_context`)
Under macOS Darwin, returning normally from a signal handler executes Darwin's internal `_sigtramp`, which calls the private Mach system call `__sigreturn`.
- **The Child Process Trap**: When a guest program calls `fork()` or `clone()` from inside a syscall handler, the child process inherits the signal frame. When the child process attempts to `__sigreturn`, the Darwin XNU kernel detects mismatched signal frame security cookies and terminates the process with `brk #1`.
- **The Solution**: LTM bypasses `_sigtramp` and `__sigreturn` completely using a custom assembly context restoration stub:
  ```assembly
  .global _ltm_resume_context
  _ltm_resume_context:
      // x0 points to Darwin __darwin_arm_thread_state64
      // Restores x1..x30, SP, and jumps to PC directly via x0
  ```

### 4.4 Real-Time Signal Delivery Engine (`rt_sigaction` / `rt_sigreturn`)
Linux applications (e.g. `ping`, `bash`) register signal handlers via `rt_sigaction` (syscall 134) and expect signals (like `SIGALRM` or `SIGINT`) to interrupt blocking syscalls.
1. **Signal Registration**: `handle_rt_sigaction` maps guest actions to Darwin host signals using `SA_SIGINFO | SA_NODEFER`.
2. **Signal Frame Delivery**: When a Darwin host signal occurs:
   - `ltm_host_signal_handler` pushes a `struct ltm_sigframe` (containing the complete `arm_thread_state64_t` register snapshot and a magic cookie `0x4c544d5f534947ULL`) onto the guest stack.
   - Sets `x0 = signum`, `lr = _ltm_sigreturn_stub`, `pc = handler`.
   - Unmasks signals using `pthread_sigmask()`.
   - Directly resumes the guest handler using `ltm_resume_context`.
3. **Signal Return Stub (`_ltm_sigreturn_stub`)**:
   - Defined in assembly:
     ```assembly
     .global _ltm_sigreturn_stub
     _ltm_sigreturn_stub:
         mov x8, #139     // LINUX_SYS_rt_sigreturn
         brk #0
     ```
   - When the guest signal handler returns to `lr`, it triggers syscall 139 (`rt_sigreturn`).
   - `handle_rt_sigreturn` validates the stack frame cookie, restores the saved register state, unmasks thread signals, and resumes original execution without any Darwin signal cookie conflicts.

---

## 5. Networking & ICMP Decapsulation Engine

### 5.1 Socket Translation
LTM maps POSIX networking syscalls to macOS BSD APIs:
- Translates `struct sockaddr_in`, `sockaddr_in6`, and `sockaddr_un` bidirectional structures.
- Handles non-blocking flags (`O_NONBLOCK`) and close-on-exec (`FD_CLOEXEC`) on `socket` (198), `socketpair` (199), and `accept` (202).
- Translates `sendmsg` (211) and `recvmsg` (212) message headers (`msghdr` and `iovec`).

### 5.2 ICMP Header Decapsulation (`got ICMP 69` Fix)
On Linux, `SOCK_DGRAM IPPROTO_ICMP` sockets strip the IPv4 header in kernel space and deliver only the ICMP payload (byte 0 is ICMP Type `0x00` Echo Reply). On macOS Darwin, datagram/raw ICMP sockets return the raw 20-byte IPv4 packet header in `recvfrom()` and `recvmsg()`.
- Byte 0 of IPv4 is `0x45` (decimal 69). Unhandled, Linux `ping` displays `got ICMP 69 (unknown ICMP type)` and drops all replies.
- **LTM Engine**:
  1. Tracks ICMP socket descriptors in a global table `g_sock_is_icmp[]` across `socket`, `dup`, `dup2/3`, `fcntl(F_DUPFD)`, and `close`.
  2. In `handle_recvfrom()` and `handle_recvmsg()`, inspects incoming packets. If an ICMP socket or IPv4 ICMP packet is detected (`(p[0] >> 4) == 4 && p[9] == 1`), the 20-byte IPv4 header is stripped via `memmove()` and byte count adjusted.
  3. Linux `ping` receives pure ICMP Echo Replies with 0% packet loss.

### 5.3 Socket Option Translation
Linux and Darwin diverge on `IPPROTO_IP` socket option constants:
- Mapped in `translate_linux_to_darwin_sockopt()`: `IP_TOS`, `IP_TTL`, `IP_HDRINCL`, `IP_OPTIONS`, `IP_RECVOPTS`, `IP_RETOPTS`, `IP_PKTINFO`, `IP_RECVTTL`, `IP_RECVTOS`, multicast options.
- Benign advisory metadata options (`IP_RECVTTL`, `IP_RETOPTS`, `IP_RECVOPTS`) are absorbed in `handle_setsockopt` if Darwin returns `EINVAL`.

---

## 6. Terminal, TTY & Interactive Shell Subsystem

### 6.1 The 36-Byte Linux <-> 72-Byte Darwin Termios Invariant
Linux ARM64 `struct termios` is **36 bytes** (with 32-bit `tcflag_t`). Darwin `struct termios` is **72 bytes** (with 64-bit `unsigned long` flags).
- Passing a guest pointer directly to Darwin `tcgetattr()` or `tcsetattr()` causes a **36-byte stack overflow**, corrupting local guest registers.
- LTM bridges terminal ioctls using `copy_darwin_to_linux_termios()` and `copy_linux_to_darwin_termios()`.

### 6.2 Terminal Job Control & SIGTTOU Isolation
When interactive shells configure process groups via `ioctl(TIOCSPGRP)`:
- Background processes trigger Darwin's kernel `SIGTTOU`, suspending the shell.
- LTM temporarily ignores `SIGTTOU` during `tcsetpgrp()`, allowing smooth job control transitions.

---

## 7. Live Host Telemetry Engine

LTM bridges authentic Apple Silicon diagnostics into virtual `/proc` and `/sys` interfaces:
- **Processor**: `sysctlbyname("machdep.cpu.brand_string")` (`Apple M3`), `sysctlbyname("hw.ncpu")` (8 cores).
- **Live Memory**: Queried dynamically from Mach VM kernel APIs:
  `host_statistics64(host, HOST_VM_INFO64, ...)` providing real-time `MemTotal`, `MemFree`, `MemAvailable`, active/inactive page metrics, and `vm.swapusage`.
- **System Uptime & Load**: `sysctlbyname("kern.boottime")` and `sysctlbyname("vm.loadavg")`.
- **Model Identifier**: `sysctlbyname("hw.model")` (e.g. `Mac15,13`) populated into `/sys/class/dmi/id/product_name`.

---

## 8. Filesystem Sandboxing & Chroot Containment

All guest path operations (`openat`, `stat`, `newfstatat`, `execve`, `chdir`) pass through `sandbox_resolve_path()`:
1. **Working Directory Isolation**: Tracks `AT_FDCWD` relative to guest directory.
2. **Chroot Prefixing**: Maps container paths (`/bin`, `/etc`, `/usr`) into `host_rootfs`.
3. **Symlink Traversal**: Follows relative and absolute links safely within `host_rootfs` (depth 32).
4. **Breakout Defense**: Canonicalizes paths and strictly blocks host system directories (`/System`, `/Library`, `/Users`, `/private`, `/etc`) with `-ENOENT` / `-EACCES`.
5. **Virtual Mounts**: Intercepts `/proc`, `/sys`, and `/dev` to route to dynamic virtual memory generators.

---

## 9. Verification & Performance

- **Regression Test Suite**: Automated 6-part test suite validating ELF loading, Linux ABI stack structure, sandboxed I/O, penetration defense against directory escapes, virtual procfs, and dynamic memory allocations (`./run_tests.sh`).
- **Interactive Shells**: Verified with GNU Bash 5.2 and BusyBox `/bin/sh` supporting command history, tab completion, pipelines, job control, and signal handling.
- **Hardware Telemetry**: Verified with `fastfetch` displaying dynamic Apple M3 CPU cores, 16KB page-based memory usage, uptime, and Alpine Linux branding.
- **Networking**: Verified with `ping` (0% packet loss) and `wget` (HTTP/HTTPS TLS 1.3).
