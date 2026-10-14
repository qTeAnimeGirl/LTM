#include "syscalls.h"

static void *g_alt_stack = NULL;
#define ALT_STACK_SIZE (1024 * 1024) /* 1MB dedicated signal stack */

static void ltm_sig_handler(int sig, siginfo_t *si, void *ctx) {
    (void)sig;
    (void)si;
    ucontext_t *uc = (ucontext_t *)ctx;
    arm_thread_state64_t *state = &uc->uc_mcontext->__ss;

    uint64_t num = state->__x[8];
    uint64_t a0 = state->__x[0];
    uint64_t a1 = state->__x[1];
    uint64_t a2 = state->__x[2];
    uint64_t a3 = state->__x[3];
    uint64_t a4 = state->__x[4];
    uint64_t a5 = state->__x[5];

    int64_t ret = syscall_dispatch(num, a0, a1, a2, a3, a4, a5);

    /* Set guest return value in x0 */
    state->__x[0] = (uint64_t)ret;

    /* Advance PC past brk #0 (4 bytes) */
    state->__pc += 4;
}

static void ltm_crash_handler(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = (ucontext_t *)ctx;
    arm_thread_state64_t *state = &uc->uc_mcontext->__ss;

    fprintf(stderr, "\n[LTM:CRASH] Fatal signal %d (%s) at address %p\n",
            sig, sig == SIGSEGV ? "SIGSEGV" : (sig == SIGBUS ? "SIGBUS" : "UNKNOWN"),
            si->si_addr);
    fprintf(stderr, "  pc  = 0x%016llx\n", (unsigned long long)state->__pc);
    fprintf(stderr, "  sp  = 0x%016llx\n", (unsigned long long)state->__sp);
    fprintf(stderr, "  lr  = 0x%016llx\n", (unsigned long long)state->__lr);
    fprintf(stderr, "  x0  = 0x%016llx\n", (unsigned long long)state->__x[0]);
    fprintf(stderr, "  x1  = 0x%016llx\n", (unsigned long long)state->__x[1]);
    fprintf(stderr, "  x2  = 0x%016llx\n", (unsigned long long)state->__x[2]);
    fprintf(stderr, "  x8  = 0x%016llx\n", (unsigned long long)state->__x[8]);
    exit(128 + sig);
}

int syscall_dispatcher_init(void) {
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

    /* Also register SIGSYS in case of unpatched svc */
    if (sigaction(SIGSYS, &sa, NULL) != 0) {
        ltm_log_error("Failed to register SIGSYS handler: %s", strerror(errno));
        return -1;
    }

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
                         uint64_t a3, uint64_t a4, uint64_t a5) {
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
        case LINUX_SYS_ppoll:
            ret = handle_ppoll((struct pollfd *)a0, (nfds_t)a1, (const struct linux_timespec *)a2, (const void *)a3);
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
        case LINUX_SYS_gettid:
            ret = handle_gettid();
            break;
        case LINUX_SYS_uname:
            ret = handle_uname((struct linux_utsname *)a0);
            break;
        case LINUX_SYS_sysinfo:
            ret = handle_sysinfo((struct linux_sysinfo *)a0);
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
            ret = handle_clone(a0, (void *)a1, (int *)a2, (void *)a3, (int *)a4);
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
