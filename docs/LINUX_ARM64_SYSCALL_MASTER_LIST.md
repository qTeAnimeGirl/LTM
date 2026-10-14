# Linux ARM64 (AArch64) Complete System Call Master Registry

**Kernel Target**: Linux 6.6+ UAPI / ARM64 (`asm-generic/unistd.h`)  
**Total Valid Syscalls**: 316 System Calls (Numbers 0 to 462 with architecture gaps)  
**Translation Runtime**: LTM (Linux To Mac)  
**Host Target**: macOS Apple Silicon Darwin ARM64  

---

## 📊 Summary by Status

| Status Category | Count | Percentage |
|:---|:---:|:---:|
| ✅ **Implemented & Verified (Wave 1 & Baseline)** | 110 | 34.8% |
| 🚀 **Wave 2 In-Progress** | 18 | 5.7% |
| 📋 **Planned (Wave 3+)** | 188 | 59.5% |
| **Total Official Linux ARM64 Syscalls** | **316** | **100.0%** |

---

## 📑 Complete System Call Catalog (0 to 462)

| Syscall # | Canonical Name | Status in LTM | Darwin Host Mapping / Implementation Strategy |
|:---:|:---|:---:|:---|
| 0 | `io_setup` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 1 | `io_destroy` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 2 | `io_submit` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 3 | `io_cancel` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 4 | `io_getevents` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 5 | `setxattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 6 | `lsetxattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 7 | `fsetxattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 8 | `getxattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 9 | `lgetxattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 10 | `fgetxattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 11 | `listxattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 12 | `llistxattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 13 | `flistxattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 14 | `removexattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 15 | `lremovexattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 16 | `fremovexattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 17 | `getcwd` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 18 | `lookup_dcookie` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 19 | `eventfd2` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 20 | `epoll_create1` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 21 | `epoll_ctl` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 22 | `epoll_pwait` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 23 | `dup` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 24 | `dup3` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 25 | `fcntl` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 26 | `inotify_init1` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 27 | `inotify_add_watch` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 28 | `inotify_rm_watch` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 29 | `ioctl` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 30 | `ioprio_set` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 31 | `ioprio_get` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 32 | `flock` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 33 | `mknodat` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 34 | `mkdirat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 35 | `unlinkat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 36 | `symlinkat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 37 | `linkat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 38 | `renameat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 39 | `umount2` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 40 | `mount` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 41 | `pivot_root` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 42 | `nfsservctl` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 43 | `statfs` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 44 | `fstatfs` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 45 | `truncate` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 46 | `ftruncate` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 47 | `fallocate` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 48 | `faccessat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 49 | `chdir` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 50 | `fchdir` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 51 | `chroot` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 52 | `fchmod` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 53 | `fchmodat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 54 | `fchownat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 55 | `fchown` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 56 | `openat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 57 | `close` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 58 | `vhangup` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 59 | `pipe2` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 60 | `quotactl` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 61 | `getdents64` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 62 | `lseek` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 63 | `read` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 64 | `write` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 65 | `readv` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 66 | `writev` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 67 | `pread64` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 68 | `pwrite64` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 69 | `preadv` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 70 | `pwritev` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 71 | `sendfile` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 72 | `pselect6` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 73 | `ppoll` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 74 | `signalfd4` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 75 | `vmsplice` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 76 | `splice` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 77 | `tee` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 78 | `readlinkat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 79 | `newfstatat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 80 | `fstat` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 81 | `sync` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 82 | `fsync` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 83 | `fdatasync` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 84 | `sync_file_range` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 85 | `timerfd_create` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 86 | `timerfd_settime` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 87 | `timerfd_gettime` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 88 | `utimensat` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 89 | `acct` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 90 | `capget` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 91 | `capset` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 92 | `personality` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 93 | `exit` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 94 | `exit_group` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 95 | `waitid` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 96 | `set_tid_address` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 97 | `unshare` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 98 | `futex` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 99 | `set_robust_list` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 100 | `get_robust_list` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 101 | `nanosleep` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 102 | `getitimer` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 103 | `setitimer` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 104 | `kexec_load` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 105 | `init_module` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 106 | `delete_module` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 107 | `timer_create` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 108 | `timer_gettime` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 109 | `timer_getoverrun` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 110 | `timer_settime` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 111 | `timer_delete` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 112 | `clock_settime` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 113 | `clock_gettime` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 114 | `clock_getres` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 115 | `clock_nanosleep` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 116 | `syslog` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 117 | `ptrace` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 118 | `sched_setparam` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 119 | `sched_setscheduler` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 120 | `sched_getscheduler` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 121 | `sched_getparam` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 122 | `sched_setaffinity` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 123 | `sched_getaffinity` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 124 | `sched_yield` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 125 | `sched_get_priority_max` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 126 | `sched_get_priority_min` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 127 | `sched_rr_get_interval` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 128 | `restart_syscall` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 129 | `kill` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 130 | `tkill` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 131 | `tgkill` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 132 | `sigaltstack` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 133 | `rt_sigsuspend` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 134 | `rt_sigaction` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 135 | `rt_sigprocmask` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 136 | `rt_sigpending` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 137 | `rt_sigtimedwait` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 138 | `rt_sigqueueinfo` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 139 | `rt_sigreturn` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 140 | `setpriority` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 141 | `getpriority` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 142 | `reboot` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 143 | `setregid` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 144 | `setgid` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 145 | `setreuid` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 146 | `setuid` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 147 | `setresuid` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 148 | `getresuid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 149 | `setresgid` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 150 | `getresgid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 151 | `setfsuid` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 152 | `setfsgid` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 153 | `times` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 154 | `setpgid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 155 | `getpgid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 156 | `getsid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 157 | `setsid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 158 | `getgroups` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 159 | `setgroups` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 160 | `uname` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 161 | `sethostname` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 162 | `setdomainname` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 163 | `getrlimit` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 164 | `setrlimit` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 165 | `getrusage` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 166 | `umask` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 167 | `prctl` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 168 | `getcpu` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 169 | `gettimeofday` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 170 | `settimeofday` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 171 | `adjtimex` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 172 | `getpid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 173 | `getppid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 174 | `getuid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 175 | `geteuid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 176 | `getgid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 177 | `getegid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 178 | `gettid` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 179 | `sysinfo` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 180 | `mq_open` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 181 | `mq_unlink` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 182 | `mq_timedsend` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 183 | `mq_timedreceive` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 184 | `mq_notify` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 185 | `mq_getsetattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 186 | `msgget` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 187 | `msgctl` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 188 | `msgrcv` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 189 | `msgsnd` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 190 | `semget` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 191 | `semctl` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 192 | `semtimedop` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 193 | `semop` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 194 | `shmget` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 195 | `shmctl` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 196 | `shmat` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 197 | `shmdt` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 198 | `socket` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 199 | `socketpair` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 200 | `bind` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 201 | `listen` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 202 | `accept` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 203 | `connect` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 204 | `getsockname` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 205 | `getpeername` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 206 | `sendto` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 207 | `recvfrom` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 208 | `setsockopt` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 209 | `getsockopt` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 210 | `shutdown` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 211 | `sendmsg` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 212 | `recvmsg` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 213 | `readahead` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 214 | `brk` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 215 | `munmap` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 216 | `mremap` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 217 | `add_key` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 218 | `request_key` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 219 | `keyctl` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 220 | `clone` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 221 | `execve` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 222 | `mmap` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 223 | `fadvise64` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 224 | `swapon` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 225 | `swapoff` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 226 | `mprotect` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 227 | `msync` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 228 | `mlock` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 229 | `munlock` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 230 | `mlockall` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 231 | `munlockall` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 232 | `mincore` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 233 | `madvise` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 234 | `remap_file_pages` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 235 | `mbind` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 236 | `get_mempolicy` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 237 | `set_mempolicy` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 238 | `migrate_pages` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 239 | `move_pages` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 240 | `rt_tgsigqueueinfo` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 241 | `perf_event_open` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 242 | `accept4` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 243 | `recvmmsg` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 260 | `wait4` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 261 | `prlimit64` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 262 | `fanotify_init` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 263 | `fanotify_mark` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 264 | `name_to_handle_at` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 265 | `open_by_handle_at` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 266 | `clock_adjtime` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 267 | `syncfs` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 268 | `setns` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 269 | `sendmmsg` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 270 | `process_vm_readv` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 271 | `process_vm_writev` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 272 | `kcmp` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 273 | `finit_module` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 274 | `sched_setattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 275 | `sched_getattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 276 | `renameat2` | ✅ Implemented | Native Darwin Mach/BSD translation + 3-way test suite |
| 277 | `seccomp` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 278 | `getrandom` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 279 | `memfd_create` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 280 | `bpf` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 281 | `execveat` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 282 | `userfaultfd` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 283 | `membarrier` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 284 | `mlock2` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 285 | `copy_file_range` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 286 | `preadv2` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 287 | `pwritev2` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 288 | `pkey_mprotect` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 289 | `pkey_alloc` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 290 | `pkey_free` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 291 | `statx` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 293 | `rseq` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 424 | `pidfd_send_signal` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 425 | `io_uring_setup` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 426 | `io_uring_enter` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 427 | `io_uring_register` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 428 | `open_tree` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 429 | `move_mount` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 430 | `fsopen` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 431 | `fsconfig` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 432 | `fsmount` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 433 | `fspick` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 434 | `pidfd_open` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 435 | `clone3` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 436 | `close_range` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 437 | `openat2` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 438 | `pidfd_getfd` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 439 | `faccessat2` | 🚀 Wave 2 In-Progress | Scheduled for immediate implementation & 3-way verification |
| 440 | `process_madvise` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 441 | `epoll_pwait2` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 442 | `mount_setattr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 443 | `quotactl_fd` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 444 | `landlock_create_ruleset` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 445 | `landlock_add_rule` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 446 | `landlock_restrict_self` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 447 | `memfd_secret` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 448 | `process_mrelease` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 449 | `futex_waitv` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 450 | `set_mempolicy_home_node` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 451 | `cachestat` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 452 | `fchmodat2` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 453 | `map_shadow_stack` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 454 | `futex_wake` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 455 | `futex_wait` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 456 | `futex_requeue` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 457 | `statmount` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 458 | `listmount` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 459 | `lsm_get_self_attr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 460 | `lsm_set_self_attr` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 461 | `lsm_list_modules` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
| 462 | `mseal` | 📋 Planned | Mapped to Darwin API or benign emulation stub |
