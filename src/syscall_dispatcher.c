#include "linux_syscall_table.h"
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

    /* Ignore background terminal signals to prevent background job control suspension */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

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


/* Syscall Name Lookup Table */
static const char * const g_linux_syscall_names[LINUX_MAX_SYSCALL_NR + 1] = {
    [0] = "io_setup",
    [1] = "io_destroy",
    [2] = "io_submit",
    [3] = "io_cancel",
    [4] = "io_getevents",
    [5] = "setxattr",
    [6] = "lsetxattr",
    [7] = "fsetxattr",
    [8] = "getxattr",
    [9] = "lgetxattr",
    [10] = "fgetxattr",
    [11] = "listxattr",
    [12] = "llistxattr",
    [13] = "flistxattr",
    [14] = "removexattr",
    [15] = "lremovexattr",
    [16] = "fremovexattr",
    [17] = "getcwd",
    [18] = "lookup_dcookie",
    [19] = "eventfd2",
    [20] = "epoll_create1",
    [21] = "epoll_ctl",
    [22] = "epoll_pwait",
    [23] = "dup",
    [24] = "dup3",
    [25] = "fcntl",
    [26] = "inotify_init1",
    [27] = "inotify_add_watch",
    [28] = "inotify_rm_watch",
    [29] = "ioctl",
    [30] = "ioprio_set",
    [31] = "ioprio_get",
    [32] = "flock",
    [33] = "mknodat",
    [34] = "mkdirat",
    [35] = "unlinkat",
    [36] = "symlinkat",
    [37] = "linkat",
    [38] = "renameat",
    [39] = "umount2",
    [40] = "mount",
    [41] = "pivot_root",
    [42] = "nfsservctl",
    [43] = "statfs",
    [44] = "fstatfs",
    [45] = "truncate",
    [46] = "ftruncate",
    [47] = "fallocate",
    [48] = "faccessat",
    [49] = "chdir",
    [50] = "fchdir",
    [51] = "chroot",
    [52] = "fchmod",
    [53] = "fchmodat",
    [54] = "fchownat",
    [55] = "fchown",
    [56] = "openat",
    [57] = "close",
    [58] = "vhangup",
    [59] = "pipe2",
    [60] = "quotactl",
    [61] = "getdents64",
    [62] = "lseek",
    [63] = "read",
    [64] = "write",
    [65] = "readv",
    [66] = "writev",
    [67] = "pread64",
    [68] = "pwrite64",
    [69] = "preadv",
    [70] = "pwritev",
    [71] = "sendfile",
    [72] = "pselect6",
    [73] = "ppoll",
    [74] = "signalfd4",
    [75] = "vmsplice",
    [76] = "splice",
    [77] = "tee",
    [78] = "readlinkat",
    [79] = "newfstatat",
    [80] = "fstat",
    [81] = "sync",
    [82] = "fsync",
    [83] = "fdatasync",
    [84] = "sync_file_range",
    [85] = "timerfd_create",
    [86] = "timerfd_settime",
    [87] = "timerfd_gettime",
    [88] = "utimensat",
    [89] = "acct",
    [90] = "capget",
    [91] = "capset",
    [92] = "personality",
    [93] = "exit",
    [94] = "exit_group",
    [95] = "waitid",
    [96] = "set_tid_address",
    [97] = "unshare",
    [98] = "futex",
    [99] = "set_robust_list",
    [100] = "get_robust_list",
    [101] = "nanosleep",
    [102] = "getitimer",
    [103] = "setitimer",
    [104] = "kexec_load",
    [105] = "init_module",
    [106] = "delete_module",
    [107] = "timer_create",
    [108] = "timer_gettime",
    [109] = "timer_getoverrun",
    [110] = "timer_settime",
    [111] = "timer_delete",
    [112] = "clock_settime",
    [113] = "clock_gettime",
    [114] = "clock_getres",
    [115] = "clock_nanosleep",
    [116] = "syslog",
    [117] = "ptrace",
    [118] = "sched_setparam",
    [119] = "sched_setscheduler",
    [120] = "sched_getscheduler",
    [121] = "sched_getparam",
    [122] = "sched_setaffinity",
    [123] = "sched_getaffinity",
    [124] = "sched_yield",
    [125] = "sched_get_priority_max",
    [126] = "sched_get_priority_min",
    [127] = "sched_rr_get_interval",
    [128] = "restart_syscall",
    [129] = "kill",
    [130] = "tkill",
    [131] = "tgkill",
    [132] = "sigaltstack",
    [133] = "rt_sigsuspend",
    [134] = "rt_sigaction",
    [135] = "rt_sigprocmask",
    [136] = "rt_sigpending",
    [137] = "rt_sigtimedwait",
    [138] = "rt_sigqueueinfo",
    [139] = "rt_sigreturn",
    [140] = "setpriority",
    [141] = "getpriority",
    [142] = "reboot",
    [143] = "setregid",
    [144] = "setgid",
    [145] = "setreuid",
    [146] = "setuid",
    [147] = "setresuid",
    [148] = "getresuid",
    [149] = "setresgid",
    [150] = "getresgid",
    [151] = "setfsuid",
    [152] = "setfsgid",
    [153] = "times",
    [154] = "setpgid",
    [155] = "getpgid",
    [156] = "getsid",
    [157] = "setsid",
    [158] = "getgroups",
    [159] = "setgroups",
    [160] = "uname",
    [161] = "sethostname",
    [162] = "setdomainname",
    [163] = "getrlimit",
    [164] = "setrlimit",
    [165] = "getrusage",
    [166] = "umask",
    [167] = "prctl",
    [168] = "getcpu",
    [169] = "gettimeofday",
    [170] = "settimeofday",
    [171] = "adjtimex",
    [172] = "getpid",
    [173] = "getppid",
    [174] = "getuid",
    [175] = "geteuid",
    [176] = "getgid",
    [177] = "getegid",
    [178] = "gettid",
    [179] = "sysinfo",
    [180] = "mq_open",
    [181] = "mq_unlink",
    [182] = "mq_timedsend",
    [183] = "mq_timedreceive",
    [184] = "mq_notify",
    [185] = "mq_getsetattr",
    [186] = "msgget",
    [187] = "msgctl",
    [188] = "msgrcv",
    [189] = "msgsnd",
    [190] = "semget",
    [191] = "semctl",
    [192] = "semtimedop",
    [193] = "semop",
    [194] = "shmget",
    [195] = "shmctl",
    [196] = "shmat",
    [197] = "shmdt",
    [198] = "socket",
    [199] = "socketpair",
    [200] = "bind",
    [201] = "listen",
    [202] = "accept",
    [203] = "connect",
    [204] = "getsockname",
    [205] = "getpeername",
    [206] = "sendto",
    [207] = "recvfrom",
    [208] = "setsockopt",
    [209] = "getsockopt",
    [210] = "shutdown",
    [211] = "sendmsg",
    [212] = "recvmsg",
    [213] = "readahead",
    [214] = "brk",
    [215] = "munmap",
    [216] = "mremap",
    [217] = "add_key",
    [218] = "request_key",
    [219] = "keyctl",
    [220] = "clone",
    [221] = "execve",
    [222] = "mmap",
    [223] = "fadvise64",
    [224] = "swapon",
    [225] = "swapoff",
    [226] = "mprotect",
    [227] = "msync",
    [228] = "mlock",
    [229] = "munlock",
    [230] = "mlockall",
    [231] = "munlockall",
    [232] = "mincore",
    [233] = "madvise",
    [234] = "remap_file_pages",
    [235] = "mbind",
    [236] = "get_mempolicy",
    [237] = "set_mempolicy",
    [238] = "migrate_pages",
    [239] = "move_pages",
    [240] = "rt_tgsigqueueinfo",
    [241] = "perf_event_open",
    [242] = "accept4",
    [243] = "recvmmsg",
    [244] = NULL,
    [245] = NULL,
    [246] = NULL,
    [247] = NULL,
    [248] = NULL,
    [249] = NULL,
    [250] = NULL,
    [251] = NULL,
    [252] = NULL,
    [253] = NULL,
    [254] = NULL,
    [255] = NULL,
    [256] = NULL,
    [257] = NULL,
    [258] = NULL,
    [259] = NULL,
    [260] = "wait4",
    [261] = "prlimit64",
    [262] = "fanotify_init",
    [263] = "fanotify_mark",
    [264] = "name_to_handle_at",
    [265] = "open_by_handle_at",
    [266] = "clock_adjtime",
    [267] = "syncfs",
    [268] = "setns",
    [269] = "sendmmsg",
    [270] = "process_vm_readv",
    [271] = "process_vm_writev",
    [272] = "kcmp",
    [273] = "finit_module",
    [274] = "sched_setattr",
    [275] = "sched_getattr",
    [276] = "renameat2",
    [277] = "seccomp",
    [278] = "getrandom",
    [279] = "memfd_create",
    [280] = "bpf",
    [281] = "execveat",
    [282] = "userfaultfd",
    [283] = "membarrier",
    [284] = "mlock2",
    [285] = "copy_file_range",
    [286] = "preadv2",
    [287] = "pwritev2",
    [288] = "pkey_mprotect",
    [289] = "pkey_alloc",
    [290] = "pkey_free",
    [291] = "statx",
    [292] = NULL,
    [293] = "rseq",
    [294] = NULL,
    [295] = NULL,
    [296] = NULL,
    [297] = NULL,
    [298] = NULL,
    [299] = NULL,
    [300] = NULL,
    [301] = NULL,
    [302] = NULL,
    [303] = NULL,
    [304] = NULL,
    [305] = NULL,
    [306] = NULL,
    [307] = NULL,
    [308] = NULL,
    [309] = NULL,
    [310] = NULL,
    [311] = NULL,
    [312] = NULL,
    [313] = NULL,
    [314] = NULL,
    [315] = NULL,
    [316] = NULL,
    [317] = NULL,
    [318] = NULL,
    [319] = NULL,
    [320] = NULL,
    [321] = NULL,
    [322] = NULL,
    [323] = NULL,
    [324] = NULL,
    [325] = NULL,
    [326] = NULL,
    [327] = NULL,
    [328] = NULL,
    [329] = NULL,
    [330] = NULL,
    [331] = NULL,
    [332] = NULL,
    [333] = NULL,
    [334] = NULL,
    [335] = NULL,
    [336] = NULL,
    [337] = NULL,
    [338] = NULL,
    [339] = NULL,
    [340] = NULL,
    [341] = NULL,
    [342] = NULL,
    [343] = NULL,
    [344] = NULL,
    [345] = NULL,
    [346] = NULL,
    [347] = NULL,
    [348] = NULL,
    [349] = NULL,
    [350] = NULL,
    [351] = NULL,
    [352] = NULL,
    [353] = NULL,
    [354] = NULL,
    [355] = NULL,
    [356] = NULL,
    [357] = NULL,
    [358] = NULL,
    [359] = NULL,
    [360] = NULL,
    [361] = NULL,
    [362] = NULL,
    [363] = NULL,
    [364] = NULL,
    [365] = NULL,
    [366] = NULL,
    [367] = NULL,
    [368] = NULL,
    [369] = NULL,
    [370] = NULL,
    [371] = NULL,
    [372] = NULL,
    [373] = NULL,
    [374] = NULL,
    [375] = NULL,
    [376] = NULL,
    [377] = NULL,
    [378] = NULL,
    [379] = NULL,
    [380] = NULL,
    [381] = NULL,
    [382] = NULL,
    [383] = NULL,
    [384] = NULL,
    [385] = NULL,
    [386] = NULL,
    [387] = NULL,
    [388] = NULL,
    [389] = NULL,
    [390] = NULL,
    [391] = NULL,
    [392] = NULL,
    [393] = NULL,
    [394] = NULL,
    [395] = NULL,
    [396] = NULL,
    [397] = NULL,
    [398] = NULL,
    [399] = NULL,
    [400] = NULL,
    [401] = NULL,
    [402] = NULL,
    [403] = NULL,
    [404] = NULL,
    [405] = NULL,
    [406] = NULL,
    [407] = NULL,
    [408] = NULL,
    [409] = NULL,
    [410] = NULL,
    [411] = NULL,
    [412] = NULL,
    [413] = NULL,
    [414] = NULL,
    [415] = NULL,
    [416] = NULL,
    [417] = NULL,
    [418] = NULL,
    [419] = NULL,
    [420] = NULL,
    [421] = NULL,
    [422] = NULL,
    [423] = NULL,
    [424] = "pidfd_send_signal",
    [425] = "io_uring_setup",
    [426] = "io_uring_enter",
    [427] = "io_uring_register",
    [428] = "open_tree",
    [429] = "move_mount",
    [430] = "fsopen",
    [431] = "fsconfig",
    [432] = "fsmount",
    [433] = "fspick",
    [434] = "pidfd_open",
    [435] = "clone3",
    [436] = "close_range",
    [437] = "openat2",
    [438] = "pidfd_getfd",
    [439] = "faccessat2",
    [440] = "process_madvise",
    [441] = "epoll_pwait2",
    [442] = "mount_setattr",
    [443] = "quotactl_fd",
    [444] = "landlock_create_ruleset",
    [445] = "landlock_add_rule",
    [446] = "landlock_restrict_self",
    [447] = "memfd_secret",
    [448] = "process_mrelease",
    [449] = "futex_waitv",
    [450] = "set_mempolicy_home_node",
    [451] = "cachestat",
    [452] = "fchmodat2",
    [453] = "map_shadow_stack",
    [454] = "futex_wake",
    [455] = "futex_wait",
    [456] = "futex_requeue",
    [457] = "statmount",
    [458] = "listmount",
    [459] = "lsm_get_self_attr",
    [460] = "lsm_set_self_attr",
    [461] = "lsm_list_modules",
    [462] = "mseal",
};

const char *linux_syscall_name(int sysno) {
    if (sysno >= 0 && sysno <= LINUX_MAX_SYSCALL_NR && g_linux_syscall_names[sysno] != NULL) {
        return g_linux_syscall_names[sysno];
    }
    return "unknown";
}

int64_t syscall_dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5, void *state) {
    ltm_trace("sys_%llu [%s](0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx)",
              (unsigned long long)num, linux_syscall_name((int)num), (unsigned long long)a0, (unsigned long long)a1,
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
        case LINUX_SYS_statfs:
            ret = handle_statfs((const char *)a0, (struct linux_statfs *)a1);
            break;
        case LINUX_SYS_fstatfs:
            ret = handle_fstatfs((int)a0, (struct linux_statfs *)a1);
            break;
        case LINUX_SYS_truncate:
            ret = handle_truncate((const char *)a0, (off_t)a1);
            break;
        case LINUX_SYS_ftruncate:
            ret = handle_ftruncate((int)a0, (off_t)a1);
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
            ret = handle_madvise((void *)a0, (size_t)a1, (int)a2);
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
            ret = handle_rt_sigaction((int)a0, (const struct linux_sigaction *)a1, (struct linux_sigaction *)a2, (size_t)a3);
            break;
        case LINUX_SYS_rt_sigreturn:
            ret = handle_rt_sigreturn(state);
            break;
        case LINUX_SYS_rt_sigprocmask:
            ret = handle_rt_sigprocmask((int)a0, (const uint64_t *)a1, (uint64_t *)a2, (size_t)a3);
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
        case LINUX_SYS_getsid:
            ret = handle_getsid((pid_t)a0);
            break;
        case LINUX_SYS_setsid:
            ret = handle_setsid();
            break;
        case LINUX_SYS_times:
            ret = handle_times((struct linux_tms *)a0);
            break;
        case LINUX_SYS_getrusage:
            ret = handle_getrusage((int)a0, (struct linux_rusage *)a1);
            break;
        case LINUX_SYS_umask:
            ret = handle_umask((mode_t)a0);
            break;
        case LINUX_SYS_setuid:
        case LINUX_SYS_setgid:
            if ((int)a0 < 0) return -LINUX_EINVAL;
            ret = 0;
            break;
        case LINUX_SYS_setreuid:
        case LINUX_SYS_setregid:
            if (((int)a0 < 0 && (int)a0 != -1) || ((int)a1 < 0 && (int)a1 != -1)) return -LINUX_EINVAL;
            ret = 0;
            break;
        case LINUX_SYS_setresuid:
        case LINUX_SYS_setresgid:
            if (((int)a0 < 0 && (int)a0 != -1) || ((int)a1 < 0 && (int)a1 != -1) || ((int)a2 < 0 && (int)a2 != -1)) return -LINUX_EINVAL;
            ret = 0;
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
        case LINUX_SYS_fsync:
            ret = handle_fsync((int)a0);
            break;
        case LINUX_SYS_fdatasync:
            ret = handle_fdatasync((int)a0);
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
        case LINUX_SYS_sched_yield:
            ret = handle_sched_yield();
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
        case LINUX_SYS_readahead:
            ret = handle_readahead((int)a0, (off_t)a1, (size_t)a2);
            break;
        case LINUX_SYS_kill:
            ret = handle_kill((pid_t)a0, (int)a1);
            break;
        case LINUX_SYS_flock:
            ret = handle_flock((int)a0, (int)a1);
            break;
        case LINUX_SYS_fallocate:
            ret = handle_fallocate((int)a0, (int)a1, (off_t)a2, (off_t)a3);
            break;
        case LINUX_SYS_chroot:
            ret = handle_chroot((const char *)a0);
            break;
        case LINUX_SYS_pread64:
            ret = handle_pread64((int)a0, (void *)a1, (size_t)a2, (off_t)a3);
            break;
        case LINUX_SYS_pwrite64:
            ret = handle_pwrite64((int)a0, (const void *)a1, (size_t)a2, (off_t)a3);
            break;
        case LINUX_SYS_signalfd4:
            ret = handle_signalfd4((int)a0, (const uint64_t *)a1, (size_t)a2, (int)a3);
            break;
        case LINUX_SYS_sync:
            ret = handle_sync();
            break;
        case LINUX_SYS_syncfs:
            ret = handle_syncfs((int)a0);
            break;
        case LINUX_SYS_timerfd_create:
            ret = handle_timerfd_create((int)a0, (int)a1);
            break;
        case LINUX_SYS_timerfd_settime:
            ret = handle_timerfd_settime((int)a0, (int)a1, (const struct linux_itimerspec *)a2, (struct linux_itimerspec *)a3);
            break;
        case LINUX_SYS_timerfd_gettime:
            ret = handle_timerfd_gettime((int)a0, (struct linux_itimerspec *)a1);
            break;
        case LINUX_SYS_clock_nanosleep:
            ret = handle_clock_nanosleep((int)a0, (int)a1, (const struct linux_timespec *)a2, (struct linux_timespec *)a3);
            break;
        case LINUX_SYS_memfd_create:
            ret = handle_memfd_create((const char *)a0, (unsigned int)a1);
            break;
        case LINUX_SYS_copy_file_range:
            ret = handle_copy_file_range((int)a0, (off_t *)a1, (int)a2, (off_t *)a3, (size_t)a4, (unsigned int)a5);
            break;
        case LINUX_SYS_close_range:
            ret = handle_close_range((unsigned int)a0, (unsigned int)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_openat2:
            ret = handle_openat2((int)a0, (const char *)a1, (struct linux_open_how *)a2, (size_t)a3);
            break;
        case LINUX_SYS_faccessat2:
            ret = handle_faccessat2((int)a0, (const char *)a1, (int)a2, (int)a3);
            break;
        case LINUX_SYS_rseq:
            ret = -LINUX_ENOSYS;
            break;

        /* Wave 3 Syscalls */
        case LINUX_SYS_setxattr:
            ret = handle_setxattr((const char *)a0, (const char *)a1, (const void *)a2, (size_t)a3, (int)a4);
            break;
        case LINUX_SYS_lsetxattr:
            ret = handle_lsetxattr((const char *)a0, (const char *)a1, (const void *)a2, (size_t)a3, (int)a4);
            break;
        case LINUX_SYS_fsetxattr:
            ret = handle_fsetxattr((int)a0, (const char *)a1, (const void *)a2, (size_t)a3, (int)a4);
            break;
        case LINUX_SYS_getxattr:
            ret = handle_getxattr((const char *)a0, (const char *)a1, (void *)a2, (size_t)a3);
            break;
        case LINUX_SYS_lgetxattr:
            ret = handle_lgetxattr((const char *)a0, (const char *)a1, (void *)a2, (size_t)a3);
            break;
        case LINUX_SYS_fgetxattr:
            ret = handle_fgetxattr((int)a0, (const char *)a1, (void *)a2, (size_t)a3);
            break;
        case LINUX_SYS_listxattr:
            ret = handle_listxattr((const char *)a0, (char *)a1, (size_t)a2);
            break;
        case LINUX_SYS_llistxattr:
            ret = handle_llistxattr((const char *)a0, (char *)a1, (size_t)a2);
            break;
        case LINUX_SYS_flistxattr:
            ret = handle_flistxattr((int)a0, (char *)a1, (size_t)a2);
            break;
        case LINUX_SYS_removexattr:
            ret = handle_removexattr((const char *)a0, (const char *)a1);
            break;
        case LINUX_SYS_lremovexattr:
            ret = handle_lremovexattr((const char *)a0, (const char *)a1);
            break;
        case LINUX_SYS_fremovexattr:
            ret = handle_fremovexattr((int)a0, (const char *)a1);
            break;
        case LINUX_SYS_eventfd2:
            ret = handle_eventfd2((unsigned int)a0, (int)a1);
            break;
        case LINUX_SYS_preadv:
            ret = handle_preadv((int)a0, (const struct linux_iovec *)a1, (int)a2, (off_t)a3);
            break;
        case LINUX_SYS_pwritev:
            ret = handle_pwritev((int)a0, (const struct linux_iovec *)a1, (int)a2, (off_t)a3);
            break;
        case LINUX_SYS_sendfile:
            ret = handle_sendfile((int)a0, (int)a1, (off_t *)a2, (size_t)a3);
            break;
        case LINUX_SYS_splice:
            ret = handle_splice((int)a0, (off_t *)a1, (int)a2, (off_t *)a3, (size_t)a4, (unsigned int)a5);
            break;
        case LINUX_SYS_tee:
            ret = handle_tee((int)a0, (int)a1, (size_t)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_sync_file_range:
            ret = handle_sync_file_range((int)a0, (off_t)a1, (off_t)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_personality:
            ret = handle_personality((unsigned long)a0);
            break;
        case LINUX_SYS_clock_getres:
            ret = handle_clock_getres((int)a0, (struct linux_timespec *)a1);
            break;
        case LINUX_SYS_getrlimit:
            ret = handle_getrlimit((int)a0, (struct linux_rlimit *)a1);
            break;
        case LINUX_SYS_setrlimit:
            ret = handle_setrlimit((int)a0, (const struct linux_rlimit *)a1);
            break;
        case LINUX_SYS_prctl:
            ret = handle_prctl((int)a0, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3, (unsigned long)a4);
            break;
        case LINUX_SYS_getcpu:
            ret = handle_getcpu((unsigned *)a0, (unsigned *)a1, (void *)a2);
            break;

        /* Wave 4 Syscalls */
        case LINUX_SYS_mknodat:
            ret = handle_mknodat((int)a0, (const char *)a1, (mode_t)a2, (dev_t)a3);
            break;
        case LINUX_SYS_vhangup:
            ret = handle_vhangup();
            break;
        case LINUX_SYS_waitid:
            ret = handle_waitid((int)a0, (id_t)a1, (struct linux_siginfo *)a2, (int)a3, (void *)a4);
            break;
        case LINUX_SYS_sched_setparam:
            ret = handle_sched_setparam((pid_t)a0, (const struct linux_sched_param *)a1);
            break;
        case LINUX_SYS_sched_getparam:
            ret = handle_sched_getparam((pid_t)a0, (struct linux_sched_param *)a1);
            break;
        case LINUX_SYS_sched_setscheduler:
            ret = handle_sched_setscheduler((pid_t)a0, (int)a1, (const struct linux_sched_param *)a2);
            break;
        case LINUX_SYS_sched_getscheduler:
            ret = handle_sched_getscheduler((pid_t)a0);
            break;
        case LINUX_SYS_sched_get_priority_max:
            ret = handle_sched_get_priority_max((int)a0);
            break;
        case LINUX_SYS_sched_get_priority_min:
            ret = handle_sched_get_priority_min((int)a0);
            break;
        case LINUX_SYS_sched_rr_get_interval:
            ret = handle_sched_rr_get_interval((pid_t)a0, (struct linux_timespec *)a1);
            break;
        case LINUX_SYS_rt_sigpending:
            ret = handle_rt_sigpending((void *)a0, (size_t)a1);
            break;
        case LINUX_SYS_setpriority:
            ret = handle_setpriority((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_getpriority:
            ret = handle_getpriority((int)a0, (int)a1);
            break;
        case LINUX_SYS_setfsuid:
            ret = handle_setfsuid((uid_t)a0);
            break;
        case LINUX_SYS_setfsgid:
            ret = handle_setfsgid((gid_t)a0);
            break;
        case LINUX_SYS_sethostname:
            ret = handle_sethostname((const char *)a0, (size_t)a1);
            break;
        case LINUX_SYS_setdomainname:
            ret = handle_setdomainname((const char *)a0, (size_t)a1);
            break;
        case LINUX_SYS_mremap:
            ret = handle_mremap((unsigned long)a0, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3, (unsigned long)a4);
            break;
        case LINUX_SYS_fadvise64:
            ret = handle_fadvise64((int)a0, (off_t)a1, (off_t)a2, (int)a3);
            break;
        case LINUX_SYS_msync:
            ret = handle_msync((unsigned long)a0, (size_t)a1, (int)a2);
            break;
        case LINUX_SYS_mlock:
            ret = handle_mlock((unsigned long)a0, (size_t)a1);
            break;
        case LINUX_SYS_munlock:
            ret = handle_munlock((unsigned long)a0, (size_t)a1);
            break;
        case LINUX_SYS_mlockall:
            ret = handle_mlockall((int)a0);
            break;
        case LINUX_SYS_munlockall:
            ret = handle_munlockall();
            break;
        case LINUX_SYS_mincore:
            ret = handle_mincore((unsigned long)a0, (size_t)a1, (unsigned char *)a2);
            break;

        /* Wave 5 Syscalls */
        case LINUX_SYS_inotify_init1:
            ret = handle_inotify_init1((int)a0);
            break;
        case LINUX_SYS_inotify_add_watch:
            ret = handle_inotify_add_watch((int)a0, (const char *)a1, (uint32_t)a2);
            break;
        case LINUX_SYS_inotify_rm_watch:
            ret = handle_inotify_rm_watch((int)a0, (int)a1);
            break;
        case LINUX_SYS_ioprio_set:
            ret = handle_ioprio_set((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_ioprio_get:
            ret = handle_ioprio_get((int)a0, (int)a1);
            break;
        case LINUX_SYS_timer_create:
            ret = handle_timer_create((int)a0, (void *)a1, (int *)a2);
            break;
        case LINUX_SYS_timer_gettime:
            ret = handle_timer_gettime((int)a0, (struct linux_itimerspec *)a1);
            break;
        case LINUX_SYS_timer_getoverrun:
            ret = handle_timer_getoverrun((int)a0);
            break;
        case LINUX_SYS_timer_settime:
            ret = handle_timer_settime((int)a0, (int)a1, (const struct linux_itimerspec *)a2, (struct linux_itimerspec *)a3);
            break;
        case LINUX_SYS_timer_delete:
            ret = handle_timer_delete((int)a0);
            break;
        case LINUX_SYS_rt_sigsuspend:
            ret = handle_rt_sigsuspend((const uint64_t *)a0, (size_t)a1);
            break;
        case LINUX_SYS_rt_sigtimedwait:
            ret = handle_rt_sigtimedwait((const uint64_t *)a0, (struct linux_siginfo *)a1, (const struct linux_timespec *)a2, (size_t)a3);
            break;
        case LINUX_SYS_rt_sigqueueinfo:
            ret = handle_rt_sigqueueinfo((pid_t)a0, (int)a1, (struct linux_siginfo *)a2);
            break;
        case LINUX_SYS_msgget:
            ret = handle_msgget((key_t)a0, (int)a1);
            break;
        case LINUX_SYS_msgctl:
            ret = handle_msgctl((int)a0, (int)a1, (void *)a2);
            break;
        case LINUX_SYS_msgrcv:
            ret = handle_msgrcv((int)a0, (void *)a1, (size_t)a2, (long)a3, (int)a4);
            break;
        case LINUX_SYS_msgsnd:
            ret = handle_msgsnd((int)a0, (const void *)a1, (size_t)a2, (int)a3);
            break;
        case LINUX_SYS_semget:
            ret = handle_semget((key_t)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_semctl:
            ret = handle_semctl((int)a0, (int)a1, (int)a2, (unsigned long)a3);
            break;
        case LINUX_SYS_semop:
            ret = handle_semop((int)a0, (void *)a1, (size_t)a2);
            break;
        case LINUX_SYS_semtimedop:
            ret = handle_semtimedop((int)a0, (void *)a1, (size_t)a2, (const struct linux_timespec *)a3);
            break;
        case LINUX_SYS_execveat:
            ret = handle_execveat((int)a0, (const char *)a1, (char *const *)a2, (char *const *)a3, (int)a4);
            break;
        case LINUX_SYS_preadv2:
            ret = handle_preadv2((int)a0, (const struct linux_iovec *)a1, (int)a2, (off_t)a3, (int)a4);
            break;
        case LINUX_SYS_pwritev2:
            ret = handle_pwritev2((int)a0, (const struct linux_iovec *)a1, (int)a2, (off_t)a3, (int)a4);
            break;
        case LINUX_SYS_statx:
            ret = handle_statx((int)a0, (const char *)a1, (int)a2, (unsigned int)a3, (struct linux_statx *)a4);
            break;
        case LINUX_SYS_pidfd_open:
            ret = handle_pidfd_open((pid_t)a0, (unsigned int)a1);
            break;
        case LINUX_SYS_shmget:
            ret = handle_shmget((key_t)a0, (size_t)a1, (int)a2);
            break;
        case LINUX_SYS_shmctl:
            ret = handle_shmctl((int)a0, (int)a1, (void *)a2);
            break;
        case LINUX_SYS_shmat:
            ret = handle_shmat((int)a0, (const void *)a1, (int)a2);
            break;
        case LINUX_SYS_shmdt:
            ret = handle_shmdt((const void *)a0);
            break;
        case LINUX_SYS_mq_open:
            ret = handle_mq_open((const char *)a0, (int)a1, (mode_t)a2, (struct linux_mq_attr *)a3);
            break;
        case LINUX_SYS_mq_unlink:
            ret = handle_mq_unlink((const char *)a0);
            break;
        case LINUX_SYS_mq_timedsend:
            ret = handle_mq_timedsend((int)a0, (const char *)a1, (size_t)a2, (unsigned int)a3, (const struct linux_timespec *)a4);
            break;
        case LINUX_SYS_mq_timedreceive:
            ret = handle_mq_timedreceive((int)a0, (char *)a1, (size_t)a2, (unsigned int *)a3, (const struct linux_timespec *)a4);
            break;
        case LINUX_SYS_mq_notify:
            ret = handle_mq_notify((int)a0, (const void *)a1);
            break;
        case LINUX_SYS_mq_getsetattr:
            ret = handle_mq_getsetattr((int)a0, (const struct linux_mq_attr *)a1, (struct linux_mq_attr *)a2);
            break;
        case LINUX_SYS_sendmmsg:
            ret = handle_sendmmsg((int)a0, (struct linux_mmsghdr *)a1, (unsigned int)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_recvmmsg:
            ret = handle_recvmmsg((int)a0, (struct linux_mmsghdr *)a1, (unsigned int)a2, (unsigned int)a3, (struct linux_timespec *)a4);
            break;
        case LINUX_SYS_clock_settime:
            ret = handle_clock_settime((clockid_t)a0, (const struct linux_timespec *)a1);
            break;
        case LINUX_SYS_settimeofday:
            ret = handle_settimeofday((const struct timeval *)a0, (const struct timezone *)a1);
            break;
        case LINUX_SYS_adjtimex:
            ret = handle_adjtimex((struct linux_timex *)a0);
            break;
        case LINUX_SYS_clock_adjtime:
            ret = handle_clock_adjtime((clockid_t)a0, (struct linux_timex *)a1);
            break;
        case LINUX_SYS_mlock2:
            ret = handle_mlock2((unsigned long)a0, (size_t)a1, (int)a2);
            break;
        case LINUX_SYS_membarrier:
            ret = handle_membarrier((int)a0, (unsigned int)a1, (int)a2);
            break;
        case LINUX_SYS_userfaultfd:
            ret = handle_userfaultfd((int)a0);
            break;
        case LINUX_SYS_sched_setattr:
            ret = handle_sched_setattr((pid_t)a0, (void *)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_sched_getattr:
            ret = handle_sched_getattr((pid_t)a0, (void *)a1, (unsigned int)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_rt_tgsigqueueinfo:
            ret = handle_rt_tgsigqueueinfo((pid_t)a0, (pid_t)a1, (int)a2, (struct linux_siginfo *)a3);
            break;
        case LINUX_SYS_pidfd_send_signal:
            ret = handle_pidfd_send_signal((int)a0, (int)a1, (struct linux_siginfo *)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_pidfd_getfd:
            ret = handle_pidfd_getfd((int)a0, (int)a1, (unsigned int)a2);
            break;

        default:
            ltm_log_debug("Unhandled Linux syscall: %llu (%s)", (unsigned long long)num, linux_syscall_name((int)num));
            ret = -LINUX_ENOSYS;
            break;
    }

    ltm_trace("syscall %llu [%s](0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx) = %lld",
              (unsigned long long)num, linux_syscall_name((int)num),
              (unsigned long long)a0, (unsigned long long)a1, (unsigned long long)a2,
              (unsigned long long)a3, (unsigned long long)a4, (unsigned long long)a5,
              (long long)ret);

    return ret;
}
