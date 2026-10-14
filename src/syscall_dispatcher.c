#include "syscalls.h"

static void *g_alt_stack = NULL;
#define ALT_STACK_SIZE (1024 * 1024) /* 1MB dedicated signal stack */

static void ltm_crash_handler(int sig, siginfo_t *si, void *ctx);

static void ltm_sig_handler(int sig, siginfo_t *si, void *ctx) {
    (void)sig;
    (void)si;
    ucontext_t *uc = (ucontext_t *)ctx;
    arm_thread_state64_t *state = &uc->uc_mcontext->__ss;

    uint32_t insn = *(uint32_t *)state->__pc;

    /* Handle unpatched svc #0 (0xd4000001) if delivered via SIGILL or SIGSYS */
    if (insn == 0xd4000001U) {
        uint64_t num = state->__x[8];
        uint64_t a0 = state->__x[0];
        uint64_t a1 = state->__x[1];
        uint64_t a2 = state->__x[2];
        uint64_t a3 = state->__x[3];
        uint64_t a4 = state->__x[4];
        uint64_t a5 = state->__x[5];

        int64_t ret = syscall_dispatch(num, a0, a1, a2, a3, a4, a5, state);
        state->__x[0] = (uint64_t)ret;
        state->__pc += 4;
        return;
    }

    if (sig == SIGILL) {
        ltm_crash_handler(sig, si, ctx);
        return;
    }

    /* Check if it's our virtualized TLS trap */
    if ((insn & 0xffe0001fU) == 0xd4200000U) {
        uint32_t imm = (insn >> 5) & 0xffffU;
        uint32_t type = imm >> 8;
        uint32_t reg = imm & 0x1fU;

        if (type == 1) {
            /* Virtualized TLS read: mrs Xt, TPIDR_EL0 */
            if (reg < 31) {
                state->__x[reg] = g_ltm.guest_tls;
            }
            state->__pc += 4;
            return;
        } else if (type == 2) {
            /* Virtualized TLS write: msr TPIDR_EL0, Xt */
            if (reg < 31) {
                g_ltm.guest_tls = state->__x[reg];
            } else {
                g_ltm.guest_tls = 0; /* xzr */
            }
            state->__pc += 4;
            return;
        }
    }

    uint64_t num = state->__x[8];
    uint64_t a0 = state->__x[0];
    uint64_t a1 = state->__x[1];
    uint64_t a2 = state->__x[2];
    uint64_t a3 = state->__x[3];
    uint64_t a4 = state->__x[4];
    uint64_t a5 = state->__x[5];

    int64_t ret = syscall_dispatch(num, a0, a1, a2, a3, a4, a5, state);

    /* Set guest return value in x0 */
    state->__x[0] = (uint64_t)ret;

    /* Advance PC past brk #0 (4 bytes) */
    state->__pc += 4;
}

static void ltm_crash_handler(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = (ucontext_t *)ctx;
    arm_thread_state64_t *state = &uc->uc_mcontext->__ss;

    uint64_t cur_tpidr = 0;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(cur_tpidr));

    fprintf(stderr, "\n[LTM:CRASH] Fatal signal %d (%s) at address %p (tpidr_el0=0x%llx)\n",
            sig, sig == SIGSEGV ? "SIGSEGV" : (sig == SIGBUS ? "SIGBUS" : (sig == SIGILL ? "SIGILL" : "UNKNOWN")),
            si->si_addr, cur_tpidr);
    fprintf(stderr, "  pc  = 0x%016llx (insn=0x%08x)\n", (unsigned long long)state->__pc, *(uint32_t *)state->__pc);
    fprintf(stderr, "  sp  = 0x%016llx\n", (unsigned long long)state->__sp);
    fprintf(stderr, "  lr  = 0x%016llx\n", (unsigned long long)state->__lr);
    fprintf(stderr, "  x0  = 0x%016llx\n", (unsigned long long)state->__x[0]);
    fprintf(stderr, "  x1  = 0x%016llx\n", (unsigned long long)state->__x[1]);
    fprintf(stderr, "  x2  = 0x%016llx\n", (unsigned long long)state->__x[2]);
    fprintf(stderr, "  x8  = 0x%016llx\n", (unsigned long long)state->__x[8]);
    exit(128 + sig);
}

int syscall_dispatcher_init(void) {
    /* Unblock all signals so inherited masks across execve cannot block SIGTRAP */
    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    sigprocmask(SIG_SETMASK, &empty_mask, NULL);

    /* Allocate alternate stack for safe signal handling */
    g_alt_stack = mmap(NULL, ALT_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (g_alt_stack == MAP_FAILED) {
        ltm_log_error("Failed to allocate signal alternate stack: %s", strerror(errno));
        return -1;
    }

    stack_t ss;
    ss.ss_sp = g_alt_stack;
    ss.ss_size = ALT_STACK_SIZE;
    ss.ss_flags = 0;
    if (sigaltstack(&ss, NULL) != 0) {
        ltm_log_error("sigaltstack failed: %s", strerror(errno));
        return -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ltm_sig_handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGTRAP, &sa, NULL) != 0) {
        ltm_log_error("Failed to register SIGTRAP handler: %s", strerror(errno));
        return -1;
    }

    /* Also register SIGSYS and SIGILL in case of unpatched svc */
    sigaction(SIGSYS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);

    /* Register crash handlers for diagnostics */
    struct sigaction sa_crash;
    memset(&sa_crash, 0, sizeof(sa_crash));
    sa_crash.sa_sigaction = ltm_crash_handler;
    sa_crash.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa_crash.sa_mask);
    sigaction(SIGSEGV, &sa_crash, NULL);
    sigaction(SIGBUS, &sa_crash, NULL);

    ltm_log_debug("Syscall dispatcher initialized successfully (SIGTRAP hooked)");
    return 0;
}

int64_t syscall_dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5, void *state) {
    ltm_trace("sys_%llu(0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx)",
              (unsigned long long)num, (unsigned long long)a0, (unsigned long long)a1,
              (unsigned long long)a2, (unsigned long long)a3, (unsigned long long)a4,
              (unsigned long long)a5);

    int64_t ret = -LINUX_ENOSYS;

    switch (num) {
        case LINUX_SYS_read:
            ret = handle_read((int)a0, (void *)a1, (size_t)a2);
            break;
        case LINUX_SYS_write:
            ret = handle_write((int)a0, (const void *)a1, (size_t)a2);
            break;
        case LINUX_SYS_openat:
            ret = handle_openat((int)a0, (const char *)a1, (int)a2, (mode_t)a3);
            break;
        case LINUX_SYS_close:
            ret = handle_close((int)a0);
            break;
        case LINUX_SYS_fstat:
            ret = handle_fstat((int)a0, (struct linux_stat *)a1);
            break;
        case LINUX_SYS_newfstatat:
            ret = handle_newfstatat((int)a0, (const char *)a1, (struct linux_stat *)a2, (int)a3);
            break;
        case LINUX_SYS_lseek:
            ret = handle_lseek((int)a0, (off_t)a1, (int)a2);
            break;
        case LINUX_SYS_ioctl:
            ret = handle_ioctl((int)a0, (unsigned long)a1, (void *)a2);
            break;
        case LINUX_SYS_readv:
            ret = handle_readv((int)a0, (const struct linux_iovec *)a1, (int)a2);
            break;
        case LINUX_SYS_writev:
            ret = handle_writev((int)a0, (const struct linux_iovec *)a1, (int)a2);
            break;
        case LINUX_SYS_dup:
            ret = handle_dup((int)a0);
            break;
        case LINUX_SYS_dup3:
            ret = handle_dup3((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_fcntl:
            ret = handle_fcntl((int)a0, (int)a1, (unsigned long)a2);
            break;
        case LINUX_SYS_getdents64:
            ret = handle_getdents64((int)a0, (void *)a1, (size_t)a2);
            break;
        case LINUX_SYS_epoll_create1:
            ret = handle_epoll_create1((int)a0);
            break;
        case LINUX_SYS_epoll_ctl:
            ret = handle_epoll_ctl((int)a0, (int)a1, (int)a2, (void *)a3);
            break;
        case LINUX_SYS_epoll_pwait:
            ret = handle_epoll_pwait((int)a0, (void *)a1, (int)a2, (int)a3, (const void *)a4);
            break;
        case LINUX_SYS_ppoll:
            ret = handle_ppoll((struct pollfd *)a0, (nfds_t)a1, (const struct linux_timespec *)a2, (const void *)a3);
            break;
        case LINUX_SYS_pselect6:
            ret = handle_pselect6((int)a0, (fd_set *)a1, (fd_set *)a2, (fd_set *)a3,
                                  (const struct linux_timespec *)a4, (const void *)a5);
            break;
        case LINUX_SYS_tkill:
            ret = handle_tkill((int)a0, (int)a1);
            break;
        case LINUX_SYS_tgkill:
            ret = handle_tgkill((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_brk:
            ret = (int64_t)handle_brk((uintptr_t)a0);
            break;
        case LINUX_SYS_mmap:
            ret = (int64_t)handle_mmap((void *)a0, (size_t)a1, (int)a2, (int)a3, (int)a4, (off_t)a5);
            break;
        case LINUX_SYS_munmap:
            ret = handle_munmap((void *)a0, (size_t)a1);
            break;
        case LINUX_SYS_mprotect:
            ret = handle_mprotect((void *)a0, (size_t)a1, (int)a2);
            break;
        case LINUX_SYS_madvise:
            ret = 0; /* benign no-op */
            break;
        case LINUX_SYS_getpid:
            ret = handle_getpid();
            break;
        case LINUX_SYS_getppid:
            ret = handle_getppid();
            break;
        case LINUX_SYS_getuid:
            ret = handle_getuid();
            break;
        case LINUX_SYS_geteuid:
            ret = handle_geteuid();
            break;
        case LINUX_SYS_getgid:
            ret = handle_getgid();
            break;
        case LINUX_SYS_getegid:
            ret = handle_getegid();
            break;
        case LINUX_SYS_getresuid:
            ret = handle_getresuid((uid_t *)a0, (uid_t *)a1, (uid_t *)a2);
            break;
        case LINUX_SYS_getresgid:
            ret = handle_getresgid((gid_t *)a0, (gid_t *)a1, (gid_t *)a2);
            break;
        case LINUX_SYS_getgroups:
            ret = handle_getgroups((int)a0, (gid_t *)a1);
            break;
        case LINUX_SYS_setgroups:
            ret = handle_setgroups((size_t)a0, (const gid_t *)a1);
            break;
        case LINUX_SYS_capget:
            ret = handle_capget((void *)a0, (void *)a1);
            break;
        case LINUX_SYS_capset:
            ret = handle_capset((void *)a0, (const void *)a1);
            break;
        case LINUX_SYS_gettid:
            ret = handle_gettid();
            break;
        case LINUX_SYS_uname:
            ret = handle_uname((struct linux_utsname *)a0);
            break;
        case LINUX_SYS_sysinfo:
            ret = handle_sysinfo((struct linux_sysinfo *)a0);
            break;
        case LINUX_SYS_getitimer:
            ret = handle_getitimer((int)a0, (void *)a1);
            break;
        case LINUX_SYS_setitimer:
            ret = handle_setitimer((int)a0, (const void *)a1, (void *)a2);
            break;
        case LINUX_SYS_clock_gettime:
            ret = handle_clock_gettime((int)a0, (struct linux_timespec *)a1);
            break;
        case LINUX_SYS_gettimeofday:
            ret = handle_gettimeofday((struct linux_timeval *)a0, (struct linux_timezone *)a1);
            break;
        case LINUX_SYS_nanosleep:
            ret = handle_nanosleep((const struct linux_timespec *)a0, (struct linux_timespec *)a1);
            break;
        case LINUX_SYS_exit:
            ret = handle_exit((int)a0);
            break;
        case LINUX_SYS_exit_group:
            ret = handle_exit_group((int)a0);
            break;
        case LINUX_SYS_set_tid_address:
            ret = handle_set_tid_address((int *)a0);
            break;
        case LINUX_SYS_futex:
            ret = handle_futex((uint32_t *)a0, (int)a1, (uint32_t)a2,
                               (const struct linux_timespec *)a3, (uint32_t *)a4, (uint32_t)a5);
            break;
        case LINUX_SYS_clone:
            ret = handle_clone(a0, (void *)a1, (int *)a2, (void *)a3, (int *)a4, state);
            break;
        case LINUX_SYS_readlinkat:
            ret = handle_readlinkat((int)a0, (const char *)a1, (char *)a2, (size_t)a3);
            break;
        case LINUX_SYS_prlimit64:
            ret = handle_prlimit64((int)a0, (int)a1, (const struct linux_rlimit64 *)a2, (struct linux_rlimit64 *)a3);
            break;
        case LINUX_SYS_getrandom:
            ret = handle_getrandom((void *)a0, (size_t)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_rt_sigaction:
        case LINUX_SYS_rt_sigprocmask:
            /* Stubs for libc initialization */
            ret = 0;
            break;
        case LINUX_SYS_sigaltstack:
            ret = handle_sigaltstack((const void *)a0, (void *)a1);
            break;
        case LINUX_SYS_getcwd:
            ret = handle_getcwd((char *)a0, (size_t)a1);
            break;
        case LINUX_SYS_chdir:
            ret = handle_chdir((const char *)a0);
            break;
        case LINUX_SYS_fchdir:
            ret = handle_fchdir((int)a0);
            break;
        case LINUX_SYS_pipe2:
            ret = handle_pipe2((int *)a0, (int)a1);
            break;
        case LINUX_SYS_wait4:
            ret = handle_wait4((pid_t)a0, (int *)a1, (int)a2, (void *)a3);
            break;
        case LINUX_SYS_execve:
            ret = handle_execve((const char *)a0, (char *const *)a1, (char *const *)a2);
            break;
        case LINUX_SYS_setpgid:
            ret = handle_setpgid((pid_t)a0, (pid_t)a1);
            break;
        case LINUX_SYS_getpgid:
            ret = handle_getpgid((pid_t)a0);
            break;
        case LINUX_SYS_setuid:
        case LINUX_SYS_setgid:
        case LINUX_SYS_setreuid:
        case LINUX_SYS_setregid:
        case LINUX_SYS_setresuid:
        case LINUX_SYS_setresgid:
            ret = 0; /* Benign no-op inside container */
            break;
        case LINUX_SYS_unlinkat:
            ret = handle_unlinkat((int)a0, (const char *)a1, (int)a2);
            break;
        case LINUX_SYS_mkdirat:
            ret = handle_mkdirat((int)a0, (const char *)a1, (mode_t)a2);
            break;
        case LINUX_SYS_faccessat:
            ret = handle_faccessat((int)a0, (const char *)a1, (int)a2, (int)a3);
            break;
        case LINUX_SYS_renameat:
            ret = handle_renameat((int)a0, (const char *)a1, (int)a2, (const char *)a3);
            break;
        case LINUX_SYS_renameat2:
            ret = handle_renameat2((int)a0, (const char *)a1, (int)a2, (const char *)a3, (unsigned int)a4);
            break;
        case LINUX_SYS_symlinkat:
            ret = handle_symlinkat((const char *)a0, (int)a1, (const char *)a2);
            break;
        case LINUX_SYS_linkat:
            ret = handle_linkat((int)a0, (const char *)a1, (int)a2, (const char *)a3, (int)a4);
            break;
        case LINUX_SYS_fchmodat:
            ret = handle_fchmodat((int)a0, (const char *)a1, (mode_t)a2, (int)a3);
            break;
        case LINUX_SYS_fchmod:
            ret = handle_fchmod((int)a0, (mode_t)a1);
            break;
        case LINUX_SYS_fchownat:
            ret = handle_fchownat((int)a0, (const char *)a1, (uid_t)a2, (gid_t)a3, (int)a4);
            break;
        case LINUX_SYS_fchown:
            ret = handle_fchown((int)a0, (uid_t)a1, (gid_t)a2);
            break;
        case LINUX_SYS_utimensat:
            ret = handle_utimensat((int)a0, (const char *)a1, (const struct linux_timespec *)a2, (int)a3);
            break;
        case LINUX_SYS_sched_getaffinity:
            ret = handle_sched_getaffinity((pid_t)a0, (size_t)a1, (unsigned long *)a2);
            break;
        case LINUX_SYS_sched_setaffinity:
            ret = handle_sched_setaffinity((pid_t)a0, (size_t)a1, (const unsigned long *)a2);
            break;
        case LINUX_SYS_socket:
            ret = handle_socket((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_socketpair:
            ret = handle_socketpair((int)a0, (int)a1, (int)a2, (int *)a3);
            break;
        case LINUX_SYS_connect:
            ret = handle_connect((int)a0, (const void *)a1, (socklen_t)a2);
            break;
        case LINUX_SYS_bind:
            ret = handle_bind((int)a0, (const void *)a1, (socklen_t)a2);
            break;
        case LINUX_SYS_listen:
            ret = handle_listen((int)a0, (int)a1);
            break;
        case LINUX_SYS_accept:
            ret = handle_accept((int)a0, (void *)a1, (socklen_t *)a2);
            break;
        case LINUX_SYS_accept4:
            ret = handle_accept4((int)a0, (void *)a1, (socklen_t *)a2, (int)a3);
            break;
        case LINUX_SYS_getsockname:
            ret = handle_getsockname((int)a0, (void *)a1, (socklen_t *)a2);
            break;
        case LINUX_SYS_getpeername:
            ret = handle_getpeername((int)a0, (void *)a1, (socklen_t *)a2);
            break;
        case LINUX_SYS_sendto:
            ret = handle_sendto((int)a0, (const void *)a1, (size_t)a2, (int)a3, (const void *)a4, (socklen_t)a5);
            break;
        case LINUX_SYS_recvfrom:
            ret = handle_recvfrom((int)a0, (void *)a1, (size_t)a2, (int)a3, (void *)a4, (socklen_t *)a5);
            break;
        case LINUX_SYS_sendmsg:
            ret = handle_sendmsg((int)a0, (const void *)a1, (int)a2);
            break;
        case LINUX_SYS_recvmsg:
            ret = handle_recvmsg((int)a0, (void *)a1, (int)a2);
            break;
        case LINUX_SYS_setsockopt:
            ret = handle_setsockopt((int)a0, (int)a1, (int)a2, (const void *)a3, (socklen_t)a4);
            break;
        case LINUX_SYS_getsockopt:
            ret = handle_getsockopt((int)a0, (int)a1, (int)a2, (void *)a3, (socklen_t *)a4);
            break;
        case LINUX_SYS_shutdown:
            ret = handle_shutdown((int)a0, (int)a1);
            break;
        case LINUX_SYS_rseq:
            ret = -LINUX_ENOSYS;
            break;
        default:
            ltm_log_debug("Unhandled Linux syscall: %llu (0x%llx)", (unsigned long long)num, (unsigned long long)num);
            ret = -LINUX_ENOSYS;
            break;
    }

    ltm_trace("syscall %llu(0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx) = %lld",
              (unsigned long long)num,
              (unsigned long long)a0, (unsigned long long)a1, (unsigned long long)a2,
              (unsigned long long)a3, (unsigned long long)a4, (unsigned long long)a5,
              (long long)ret);

    return ret;
}
