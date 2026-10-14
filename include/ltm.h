#ifndef LTM_H
#define LTM_H

#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/ucontext.h>
#include <poll.h>
#include <mach/mach.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <limits.h>
#include <sys/mount.h>
#include <sys/times.h>
#include <sys/resource.h>
#include <sched.h>

/* LTM (Linux To Mac) Project Metadata */
#define LTM_PROJECT_NAME          "LTM (Linux To Mac)"
#define LTM_KERNEL_VERSION_NAME   "LTM 2026.1A"
#define LTM_KERNEL_RELEASE        "LTM 2026.1A"
#define LTM_KERNEL_FULL_VERSION   "Linux version LTM 2026.1A (ltm@apple-silicon) (clang 21.0.0) #1 SMP PREEMPT_DYNAMIC Wed Oct 14 00:00:00 UTC 2026\n"

/* Page definitions for Apple Silicon (16KB) */
#define PAGE_SIZE_16K     0x4000ULL
#define PAGE_MASK_16K     (~(0x4000ULL - 1))
#define PAGE_ROUND_DOWN(x) ((uintptr_t)(x) & PAGE_MASK_16K)
#define PAGE_ROUND_UP(x)   (((uintptr_t)(x) + 0x3fffULL) & PAGE_MASK_16K)

/* ARM64 opcodes */
#define ARM64_SVC_0       0xd4000001U
#define ARM64_BRK_0       0xd4200000U
#define ARM64_NOP         0xd503201fU

/* Linux ARM64 Syscall Numbers */
#define LINUX_SYS_io_setup            0
#define LINUX_SYS_io_destroy          1
#define LINUX_SYS_io_submit           2
#define LINUX_SYS_io_cancel           3
#define LINUX_SYS_io_getevents        4
#define LINUX_SYS_setxattr            5
#define LINUX_SYS_lsetxattr           6
#define LINUX_SYS_fsetxattr           7
#define LINUX_SYS_getxattr            8
#define LINUX_SYS_lgetxattr           9
#define LINUX_SYS_fgetxattr           10
#define LINUX_SYS_listxattr           11
#define LINUX_SYS_llistxattr          12
#define LINUX_SYS_flistxattr          13
#define LINUX_SYS_removexattr         14
#define LINUX_SYS_lremovexattr        15
#define LINUX_SYS_fremovexattr        16
#define LINUX_SYS_getcwd              17
#define LINUX_SYS_lookup_dcookie      18
#define LINUX_SYS_eventfd2            19
#define LINUX_SYS_epoll_create1       20
#define LINUX_SYS_epoll_ctl           21
#define LINUX_SYS_epoll_pwait         22
#define LINUX_SYS_dup                 23
#define LINUX_SYS_dup3                24
#define LINUX_SYS_fcntl               25
#define LINUX_SYS_inotify_init1       26
#define LINUX_SYS_inotify_add_watch   27
#define LINUX_SYS_inotify_rm_watch    28
#define LINUX_SYS_ioctl               29
#define LINUX_SYS_ioprio_set          30
#define LINUX_SYS_ioprio_get          31
#define LINUX_SYS_flock               32
#define LINUX_SYS_mknodat             33
#define LINUX_SYS_mkdirat             34
#define LINUX_SYS_unlinkat            35
#define LINUX_SYS_symlinkat           36
#define LINUX_SYS_linkat              37
#define LINUX_SYS_renameat            38
#define LINUX_SYS_umount2             39
#define LINUX_SYS_mount               40
#define LINUX_SYS_pivot_root          41
#define LINUX_SYS_nfsservctl          42
#define LINUX_SYS_statfs              43
#define LINUX_SYS_fstatfs             44
#define LINUX_SYS_truncate            45
#define LINUX_SYS_ftruncate           46
#define LINUX_SYS_fallocate           47
#define LINUX_SYS_faccessat           48
#define LINUX_SYS_chdir               49
#define LINUX_SYS_fchdir              50
#define LINUX_SYS_chroot              51
#define LINUX_SYS_fchmod              52
#define LINUX_SYS_fchmodat            53
#define LINUX_SYS_fchownat            54
#define LINUX_SYS_fchown              55
#define LINUX_SYS_openat              56
#define LINUX_SYS_close               57
#define LINUX_SYS_vhangup             58
#define LINUX_SYS_pipe2               59
#define LINUX_SYS_quotactl            60
#define LINUX_SYS_getdents64          61
#define LINUX_SYS_lseek               62
#define LINUX_SYS_read                63
#define LINUX_SYS_write               64
#define LINUX_SYS_readv               65
#define LINUX_SYS_writev              66
#define LINUX_SYS_pread64             67
#define LINUX_SYS_pwrite64            68
#define LINUX_SYS_preadv              69
#define LINUX_SYS_pwritev             70
#define LINUX_SYS_sendfile            71
#define LINUX_SYS_pselect6            72
#define LINUX_SYS_ppoll               73
#define LINUX_SYS_signalfd4           74
#define LINUX_SYS_vmsplice            75
#define LINUX_SYS_splice              76
#define LINUX_SYS_tee                 77
#define LINUX_SYS_readlinkat          78
#define LINUX_SYS_newfstatat          79
#define LINUX_SYS_fstat               80
#define LINUX_SYS_sync                81
#define LINUX_SYS_fsync               82
#define LINUX_SYS_fdatasync           83
#define LINUX_SYS_sync_file_range     84
#define LINUX_SYS_timerfd_create      85
#define LINUX_SYS_timerfd_settime     86
#define LINUX_SYS_timerfd_gettime     87
#define LINUX_SYS_utimensat           88
#define LINUX_SYS_acct                89
#define LINUX_SYS_capget              90
#define LINUX_SYS_capset              91
#define LINUX_SYS_personality         92
#define LINUX_SYS_exit                93
#define LINUX_SYS_exit_group          94
#define LINUX_SYS_waitid              95
#define LINUX_SYS_set_tid_address     96
#define LINUX_SYS_unshare             97
#define LINUX_SYS_futex               98
#define LINUX_SYS_set_robust_list     99
#define LINUX_SYS_get_robust_list     100
#define LINUX_SYS_nanosleep           101
#define LINUX_SYS_getitimer           102
#define LINUX_SYS_setitimer           103
#define LINUX_SYS_kexec_load          104
#define LINUX_SYS_init_module         105
#define LINUX_SYS_delete_module       106
#define LINUX_SYS_timer_create        107
#define LINUX_SYS_timer_gettime       108
#define LINUX_SYS_timer_getoverrun    109
#define LINUX_SYS_timer_settime       110
#define LINUX_SYS_timer_delete        111
#define LINUX_SYS_clock_settime       112
#define LINUX_SYS_clock_gettime       113
#define LINUX_SYS_clock_getres        114
#define LINUX_SYS_clock_nanosleep     115
#define LINUX_SYS_syslog              116
#define LINUX_SYS_ptrace              117
#define LINUX_SYS_sched_setparam      118
#define LINUX_SYS_sched_setscheduler  119
#define LINUX_SYS_sched_getscheduler  120
#define LINUX_SYS_sched_getparam      121
#define LINUX_SYS_sched_setaffinity   122
#define LINUX_SYS_sched_getaffinity   123
#define LINUX_SYS_sched_yield         124
#define LINUX_SYS_sched_get_priority_max 125
#define LINUX_SYS_sched_get_priority_min 126
#define LINUX_SYS_sched_rr_get_interval  127
#define LINUX_SYS_restart_syscall     128
#define LINUX_SYS_kill                129
#define LINUX_SYS_tkill               130
#define LINUX_SYS_tgkill              131
#define LINUX_SYS_sigaltstack         132
#define LINUX_SYS_rt_sigsuspend       133
#define LINUX_SYS_rt_sigaction        134
#define LINUX_SYS_rt_sigprocmask      135
#define LINUX_SYS_rt_sigpending       136
#define LINUX_SYS_rt_sigtimedwait     137
#define LINUX_SYS_rt_sigqueueinfo     138
#define LINUX_SYS_rt_sigreturn        139
#define LINUX_SYS_setpriority         140
#define LINUX_SYS_getpriority         141
#define LINUX_SYS_reboot              142
#define LINUX_SYS_setregid            143
#define LINUX_SYS_setgid              144
#define LINUX_SYS_setreuid            145
#define LINUX_SYS_setuid              146
#define LINUX_SYS_setresuid           147
#define LINUX_SYS_getresuid           148
#define LINUX_SYS_setresgid           149
#define LINUX_SYS_getresgid           150
#define LINUX_SYS_setfsuid            151
#define LINUX_SYS_setfsgid            152
#define LINUX_SYS_times               153
#define LINUX_SYS_setpgid             154
#define LINUX_SYS_getpgid             155
#define LINUX_SYS_getsid              156
#define LINUX_SYS_setsid              157
#define LINUX_SYS_getgroups           158
#define LINUX_SYS_setgroups           159
#define LINUX_SYS_uname               160
#define LINUX_SYS_sethostname         161
#define LINUX_SYS_setdomainname       162
#define LINUX_SYS_getrlimit           163
#define LINUX_SYS_setrlimit           164
#define LINUX_SYS_getrusage           165
#define LINUX_SYS_umask               166
#define LINUX_SYS_prctl               167
#define LINUX_SYS_getcpu              168
#define LINUX_SYS_gettimeofday        169
#define LINUX_SYS_settimeofday        170
#define LINUX_SYS_adjtimex            171
#define LINUX_SYS_getpid              172
#define LINUX_SYS_getppid             173
#define LINUX_SYS_getuid              174
#define LINUX_SYS_geteuid             175
#define LINUX_SYS_getgid              176
#define LINUX_SYS_getegid             177
#define LINUX_SYS_gettid              178
#define LINUX_SYS_sysinfo             179
#define LINUX_SYS_mq_open             180
#define LINUX_SYS_mq_unlink           181
#define LINUX_SYS_mq_timedsend        182
#define LINUX_SYS_mq_timedreceive     183
#define LINUX_SYS_mq_notify           184
#define LINUX_SYS_mq_getsetattr       185
#define LINUX_SYS_msgget              186
#define LINUX_SYS_msgctl              187
#define LINUX_SYS_msgrcv              188
#define LINUX_SYS_msgsnd              189
#define LINUX_SYS_semget              190
#define LINUX_SYS_semctl              191
#define LINUX_SYS_semtimedop          192
#define LINUX_SYS_semop               193
#define LINUX_SYS_shmget              194
#define LINUX_SYS_shmctl              195
#define LINUX_SYS_shmat               196
#define LINUX_SYS_shmdt               197
#define LINUX_SYS_socket              198
#define LINUX_SYS_socketpair          199
#define LINUX_SYS_bind                200
#define LINUX_SYS_listen              201
#define LINUX_SYS_accept              202
#define LINUX_SYS_connect             203
#define LINUX_SYS_getsockname         204
#define LINUX_SYS_getpeername         205
#define LINUX_SYS_sendto              206
#define LINUX_SYS_recvfrom            207
#define LINUX_SYS_setsockopt          208
#define LINUX_SYS_getsockopt          209
#define LINUX_SYS_shutdown            210
#define LINUX_SYS_sendmsg             211
#define LINUX_SYS_recvmsg             212
#define LINUX_SYS_readahead           213
#define LINUX_SYS_brk                 214
#define LINUX_SYS_munmap              215
#define LINUX_SYS_mremap              216
#define LINUX_SYS_add_key             217
#define LINUX_SYS_request_key         218
#define LINUX_SYS_keyctl              219
#define LINUX_SYS_clone               220
#define LINUX_SYS_execve              221
#define LINUX_SYS_mmap                222
#define LINUX_SYS_fadvise64           223
#define LINUX_SYS_swapon              224
#define LINUX_SYS_swapoff             225
#define LINUX_SYS_mprotect            226
#define LINUX_SYS_msync               227
#define LINUX_SYS_mlock               228
#define LINUX_SYS_munlock             229
#define LINUX_SYS_mlockall            230
#define LINUX_SYS_munlockall          231
#define LINUX_SYS_mincore             232
#define LINUX_SYS_madvise             233
#define LINUX_SYS_remap_file_pages    234
#define LINUX_SYS_mbind               235
#define LINUX_SYS_get_mempolicy       236
#define LINUX_SYS_set_mempolicy       237
#define LINUX_SYS_migrate_pages       238
#define LINUX_SYS_move_pages          239
#define LINUX_SYS_rt_tgsigqueueinfo   240
#define LINUX_SYS_perf_event_open     241
#define LINUX_SYS_accept4             242
#define LINUX_SYS_recvmmsg            243
#define LINUX_SYS_wait4               260
#define LINUX_SYS_prlimit64           261
#define LINUX_SYS_fanotify_init       262
#define LINUX_SYS_fanotify_mark       263
#define LINUX_SYS_name_to_handle_at   264
#define LINUX_SYS_open_by_handle_at   265
#define LINUX_SYS_clock_adjtime       266
#define LINUX_SYS_syncfs              267
#define LINUX_SYS_setns               268
#define LINUX_SYS_sendmmsg            269
#define LINUX_SYS_process_vm_readv    270
#define LINUX_SYS_process_vm_writev   271
#define LINUX_SYS_kcmp                272
#define LINUX_SYS_finit_module        273
#define LINUX_SYS_sched_setattr       274
#define LINUX_SYS_sched_getattr       275
#define LINUX_SYS_renameat2           276
#define LINUX_SYS_seccomp             277
#define LINUX_SYS_getrandom           278
#define LINUX_SYS_memfd_create        279
#define LINUX_SYS_bpf                 280
#define LINUX_SYS_execveat            281
#define LINUX_SYS_userfaultfd         282
#define LINUX_SYS_membarrier          283
#define LINUX_SYS_mlock2              284
#define LINUX_SYS_copy_file_range     285
#define LINUX_SYS_preadv2             286
#define LINUX_SYS_pwritev2            287
#define LINUX_SYS_pkey_mprotect       288
#define LINUX_SYS_pkey_alloc          289
#define LINUX_SYS_pkey_free           290
#define LINUX_SYS_statx               291
#define LINUX_SYS_rseq                293
#define LINUX_SYS_pidfd_send_signal   424
#define LINUX_SYS_pidfd_open          434
#define LINUX_SYS_clone3              435
#define LINUX_SYS_close_range         436
#define LINUX_SYS_openat2             437
#define LINUX_SYS_pidfd_getfd         438
#define LINUX_SYS_faccessat2          439

/* Linux Errno Values */
#define LINUX_EPERM            1
#define LINUX_ENOENT           2
#define LINUX_ESRCH            3
#define LINUX_EINTR            4
#define LINUX_EIO              5
#define LINUX_ENXIO            6
#define LINUX_E2BIG            7
#define LINUX_ENOEXEC          8
#define LINUX_EBADF            9
#define LINUX_ECHILD          10
#define LINUX_EAGAIN          11
#define LINUX_ENOMEM          12
#define LINUX_EACCES          13
#define LINUX_EFAULT          14
#define LINUX_ENOTBLK         15
#define LINUX_EBUSY           16
#define LINUX_EEXIST          17
#define LINUX_EXDEV           18
#define LINUX_ENODEV          19
#define LINUX_ENOTDIR         20
#define LINUX_EISDIR          21
#define LINUX_EINVAL          22
#define LINUX_ENFILE          23
#define LINUX_EMFILE          24
#define LINUX_ENOTTY          25
#define LINUX_ETXTBSY         26
#define LINUX_EFBIG           27
#define LINUX_ENOSPC          28
#define LINUX_ESPIPE          29
#define LINUX_EROFS           30
#define LINUX_EMLINK          31
#define LINUX_EPIPE           32
#define LINUX_EDOM            33
#define LINUX_ERANGE          34
#define LINUX_EDEADLK         35
#define LINUX_ENAMETOOLONG    36
#define LINUX_ENOLCK          37
#define LINUX_ENOSYS          38
#define LINUX_ENOTEMPTY       39
#define LINUX_ELOOP           40
#define LINUX_EWOULDBLOCK     LINUX_EAGAIN
#define LINUX_ENOMSG          42
#define LINUX_EIDRM           43
#define LINUX_ECHRNG          44
#define LINUX_EL2NSYNC        45
#define LINUX_EL3HLT          46
#define LINUX_EL3RST          47
#define LINUX_ELNRNG          48
#define LINUX_EUNATCH         49
#define LINUX_ENOCSI          50
#define LINUX_EL2HLT          51
#define LINUX_EBADE           52
#define LINUX_EBADR           53
#define LINUX_EXFULL          54
#define LINUX_ENOANO          55
#define LINUX_EBADRQC         56
#define LINUX_EBADSLT         57
#define LINUX_EDEADLOCK       LINUX_EDEADLK
#define LINUX_EBFONT          59
#define LINUX_ENOSTR          60
#define LINUX_ENODATA         61
#define LINUX_ETIME           62
#define LINUX_ENOSR           63
#define LINUX_ENONET          64
#define LINUX_ENOPKG          65
#define LINUX_EREMOTE         66
#define LINUX_ENOLINK         67
#define LINUX_EADV            68
#define LINUX_ESRMNT          69
#define LINUX_ECOMM           70
#define LINUX_EPROTO          71
#define LINUX_EMULTIHOP       72
#define LINUX_EDOTDOT         73
#define LINUX_EBADMSG         74
#define LINUX_EOVERFLOW       75
#define LINUX_ENOTUNIQ        76
#define LINUX_EBADFD          77
#define LINUX_EREMCHG         78
#define LINUX_ELIBACC         79
#define LINUX_ELIBBAD         80
#define LINUX_ELIBSCN         81
#define LINUX_ELIBMAX         82
#define LINUX_ELIBEXEC        83
#define LINUX_EILSEQ          84
#define LINUX_ERESTART        85
#define LINUX_ESTRPIPE        86
#define LINUX_EUSERS          87
#define LINUX_ENOTSOCK        88
#define LINUX_EDESTADDRREQ    89
#define LINUX_EMSGSIZE        90
#define LINUX_EPROTOTYPE      91
#define LINUX_ENOPROTOOPT     92
#define LINUX_EPROTONOSUPPORT 93
#define LINUX_ESOCKTNOSUPPORT 94
#define LINUX_EOPNOTSUPP      95
#define LINUX_EPFNOSUPPORT    96
#define LINUX_EAFNOSUPPORT    97
#define LINUX_EADDRINUSE      98
#define LINUX_EADDRNOTAVAIL   99
#define LINUX_ENETDOWN        100
#define LINUX_ENETUNREACH     101
#define LINUX_ENETRESET       102
#define LINUX_ECONNABORTED    103
#define LINUX_ECONNRESET      104
#define LINUX_ENOBUFS         105
#define LINUX_EISCONN         106
#define LINUX_ENOTCONN        107
#define LINUX_ESHUTDOWN       108
#define LINUX_ETOOMANYREFS    109
#define LINUX_ETIMEDOUT       110
#define LINUX_ECONNREFUSED    111
#define LINUX_EHOSTDOWN       112
#define LINUX_EHOSTUNREACH    113
#define LINUX_EALREADY        114
#define LINUX_EINPROGRESS     115
#define LINUX_ESTALE          116
#define LINUX_ENOKEY          126
#define LINUX_EKEYEXPIRED     127
#define LINUX_EKEYREVOKED     128
#define LINUX_EKEYREJECTED    129

/* Linux Reboot Magics */
#define LINUX_REBOOT_MAGIC1   0xfee1dead
#define LINUX_REBOOT_MAGIC2   672274793

/* Linux Auxiliary Vector Types */
#define LINUX_AT_NULL         0
#define LINUX_AT_IGNORE       1
#define LINUX_AT_EXECFD       2
#define LINUX_AT_PHDR         3
#define LINUX_AT_PHENT        4
#define LINUX_AT_PHNUM        5
#define LINUX_AT_PAGESZ       6
#define LINUX_AT_BASE         7
#define LINUX_AT_FLAGS        8
#define LINUX_AT_ENTRY        9
#define LINUX_AT_NOTELF       10
#define LINUX_AT_UID          11
#define LINUX_AT_EUID         12
#define LINUX_AT_GID          13
#define LINUX_AT_EGID         14
#define LINUX_AT_PLATFORM     15
#define LINUX_AT_HWCAP        16
#define LINUX_AT_CLKTCK       17
#define LINUX_AT_SECURE       23
#define LINUX_AT_RANDOM       25
#define LINUX_AT_HWCAP2       26
#define LINUX_AT_EXECFN       31
#define LINUX_AT_SYSINFO_EHDR 33

/* Linux ARM64 open flags & AT_FDCWD (asm-generic/fcntl.h) */
#define LINUX_AT_FDCWD        (-100)
#define LINUX_O_RDONLY        00
#define LINUX_O_WRONLY        01
#define LINUX_O_RDWR          02
#define LINUX_O_CREAT         0100       /* 0x40 */
#define LINUX_O_EXCL          0200       /* 0x80 */
#define LINUX_O_NOCTTY        0400       /* 0x100 */
#define LINUX_O_TRUNC         01000      /* 0x200 */
#define LINUX_O_APPEND        02000      /* 0x400 */
#define LINUX_O_NONBLOCK      04000      /* 0x800 */
#define LINUX_O_DSYNC         010000     /* 0x1000 */
#define LINUX_O_ASYNC         020000     /* 0x2000 */
#define LINUX_O_DIRECTORY     040000     /* 0x4000 */
#define LINUX_O_NOFOLLOW      0100000    /* 0x8000 */
#define LINUX_O_LARGEFILE     0400000    /* 0x20000 */
#define LINUX_O_CLOEXEC       02000000   /* 0x80000 */
#define LINUX_O_PATH          010000000  /* 0x200000 */

/* Linux AT_* flags */
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100
#define LINUX_AT_REMOVEDIR        0x200
#define LINUX_AT_SYMLINK_FOLLOW   0x400
#define LINUX_AT_NO_AUTOMOUNT     0x800
#define LINUX_AT_EMPTY_PATH       0x1000
#define LINUX_AT_EACCESS          0x200

/* Linux mmap flags */
#define LINUX_MAP_SHARED      0x01
#define LINUX_MAP_PRIVATE     0x02
#define LINUX_MAP_FIXED       0x10
#define LINUX_MAP_ANONYMOUS   0x20
#define LINUX_MAP_GROWSDOWN   0x0100
#define LINUX_MAP_DENYWRITE   0x0800
#define LINUX_MAP_EXECUTABLE  0x1000
#define LINUX_MAP_LOCKED      0x2000
#define LINUX_MAP_NORESERVE   0x4000
#define LINUX_MAP_POPULATE    0x8000
#define LINUX_MAP_NONBLOCK    0x10000
#define LINUX_MAP_STACK       0x20000

/* Linux futex operations */
#define LINUX_FUTEX_WAIT            0
#define LINUX_FUTEX_WAKE            1
#define LINUX_FUTEX_FD              2
#define LINUX_FUTEX_REQUEUE         3
#define LINUX_FUTEX_CMP_REQUEUE     4
#define LINUX_FUTEX_WAKE_OP         5
#define LINUX_FUTEX_LOCK_PI         6
#define LINUX_FUTEX_UNLOCK_PI       7
#define LINUX_FUTEX_TRYLOCK_PI      8
#define LINUX_FUTEX_WAIT_BITSET     9
#define LINUX_FUTEX_WAKE_BITSET     10
#define LINUX_FUTEX_WAIT_REQUEUE_PI 11
#define LINUX_FUTEX_CMP_REQUEUE_PI  12
#define LINUX_FUTEX_PRIVATE_FLAG    128
#define LINUX_FUTEX_CLOCK_REALTIME  256

/* Linux clone flags */
#define LINUX_CSIGNAL              0x000000ff
#define LINUX_CLONE_VM             0x00000100
#define LINUX_CLONE_FS             0x00000200
#define LINUX_CLONE_FILES          0x00000400
#define LINUX_CLONE_SIGHAND        0x00000800
#define LINUX_CLONE_THREAD         0x00010000
#define LINUX_CLONE_SETTLS         0x00080000
#define LINUX_CLONE_PARENT_SETTID  0x00100000
#define LINUX_CLONE_CHILD_CLEARTID 0x00200000
#define LINUX_CLONE_DETACHED       0x00400000
#define LINUX_CLONE_CHILD_SETTID   0x01000000

/* Global runtime state */
typedef struct {
    char host_root[1024];         /* Working directory / invocation root ($PWD) */
    char host_rootfs[1024];       /* Chroot rootfs directory */
    char binary_path[1024];       /* Canonical path to guest binary */
    int guest_argc;               /* Guest argc */
    char **guest_argv;            /* Guest argv */
    char **guest_envp;            /* Guest envp */
    uintptr_t elf_base;           /* Load base address of ELF */
    uintptr_t elf_entry;          /* Entry point address (interpreter or main) */
    uintptr_t interp_base;        /* Base address of dynamic linker (or 0) */
    uintptr_t main_entry;         /* Entry point of main executable */
    uintptr_t elf_ph_addr;        /* Program headers virtual address */
    uint16_t elf_ph_num;          /* Number of program headers */
    uint16_t elf_ph_ent;          /* Size of program header */
    uintptr_t brk_start;          /* Initial brk address */
    uintptr_t brk_current;        /* Current brk address */
    void *guest_stack_base;       /* Guest stack allocation base */
    size_t guest_stack_size;      /* Guest stack size (8MB) */
    void *guest_sp;               /* Top of guest stack (entry sp) */
    int *child_tid_ptr;           /* set_tid_address pointer */
    uintptr_t guest_tls;          /* Virtualized Linux TPIDR_EL0 thread pointer */
    bool debug_enabled;           /* LTM_DEBUG enabled */
    bool trace_syscalls;          /* LTM_STRACE enabled */
} ltm_runtime_t;

extern ltm_runtime_t g_ltm;

/* Logging macros */
#define ltm_log_debug(fmt, ...) do { \
    if (g_ltm.debug_enabled) { \
        fprintf(stderr, "[LTM:DEBUG] " fmt "\n", ##__VA_ARGS__); \
    } \
} while (0)

#define ltm_log_info(fmt, ...) do { \
    if (g_ltm.debug_enabled) { \
        fprintf(stderr, "[LTM:INFO] " fmt "\n", ##__VA_ARGS__); \
    } \
} while (0)

#define ltm_log_error(fmt, ...) do { \
    fprintf(stderr, "[LTM:ERROR] " fmt "\n", ##__VA_ARGS__); \
} while (0)

#define ltm_trace(fmt, ...) do { \
    if (g_ltm.trace_syscalls) { \
        fprintf(stderr, "[LTM:STRACE] " fmt "\n", ##__VA_ARGS__); \
    } \
} while (0)

/* Darwin errno -> negative Linux errno conversion */
static inline int64_t darwin_to_linux_errno(int err) {
    if (err == 0) return 0;
    switch (err) {
        case EPERM:           return -LINUX_EPERM;
        case ENOENT:          return -LINUX_ENOENT;
        case ESRCH:           return -LINUX_ESRCH;
        case EINTR:           return -LINUX_EINTR;
        case EIO:             return -LINUX_EIO;
        case ENXIO:           return -LINUX_ENXIO;
        case E2BIG:           return -LINUX_E2BIG;
        case ENOEXEC:         return -LINUX_ENOEXEC;
        case EBADF:           return -LINUX_EBADF;
        case ECHILD:          return -LINUX_ECHILD;
        case EAGAIN:          return -LINUX_EAGAIN;
        case ENOMEM:          return -LINUX_ENOMEM;
        case EACCES:          return -LINUX_EACCES;
        case EFAULT:          return -LINUX_EFAULT;
        case EBUSY:           return -LINUX_EBUSY;
        case EEXIST:          return -LINUX_EEXIST;
        case EXDEV:           return -LINUX_EXDEV;
        case ENODEV:          return -LINUX_ENODEV;
        case ENOTDIR:         return -LINUX_ENOTDIR;
        case EISDIR:          return -LINUX_EISDIR;
        case EINVAL:          return -LINUX_EINVAL;
        case ENFILE:          return -LINUX_ENFILE;
        case EMFILE:          return -LINUX_EMFILE;
        case ENOTTY:          return -LINUX_ENOTTY;
        case ETXTBSY:         return -LINUX_ETXTBSY;
        case EFBIG:           return -LINUX_EFBIG;
        case ENOSPC:          return -LINUX_ENOSPC;
        case ESPIPE:          return -LINUX_ESPIPE;
        case EROFS:           return -LINUX_EROFS;
        case EMLINK:          return -LINUX_EMLINK;
        case EPIPE:           return -LINUX_EPIPE;
        case EDOM:            return -LINUX_EDOM;
        case ERANGE:          return -LINUX_ERANGE;
        case EDEADLK:         return -LINUX_EDEADLK;
        case ENAMETOOLONG:    return -LINUX_ENAMETOOLONG;
        case ENOLCK:          return -LINUX_ENOLCK;
        case ENOSYS:          return -LINUX_ENOSYS;
        case ENOTEMPTY:       return -LINUX_ENOTEMPTY;
        case ELOOP:           return -LINUX_ELOOP;
        case EOVERFLOW:       return -LINUX_EOVERFLOW;
        case ENOTSOCK:        return -LINUX_ENOTSOCK;
        case EDESTADDRREQ:    return -LINUX_EDESTADDRREQ;
        case EMSGSIZE:        return -LINUX_EMSGSIZE;
        case EPROTOTYPE:      return -LINUX_EPROTOTYPE;
        case ENOPROTOOPT:     return -LINUX_ENOPROTOOPT;
        case EPROTONOSUPPORT: return -LINUX_EPROTONOSUPPORT;
        case ESOCKTNOSUPPORT: return -LINUX_ESOCKTNOSUPPORT;
        case ENOTSUP:         return -LINUX_EOPNOTSUPP;
        case EAFNOSUPPORT:    return -LINUX_EAFNOSUPPORT;
        case EADDRINUSE:      return -LINUX_EADDRINUSE;
        case EADDRNOTAVAIL:   return -LINUX_EADDRNOTAVAIL;
        case ENETDOWN:        return -LINUX_ENETDOWN;
        case ENETUNREACH:     return -LINUX_ENETUNREACH;
        case ENETRESET:       return -LINUX_ENETRESET;
        case ECONNABORTED:    return -LINUX_ECONNABORTED;
        case ECONNRESET:      return -LINUX_ECONNRESET;
        case ENOBUFS:         return -LINUX_ENOBUFS;
        case EISCONN:         return -LINUX_EISCONN;
        case ENOTCONN:        return -LINUX_ENOTCONN;
        case ESHUTDOWN:       return -LINUX_ESHUTDOWN;
        case ETOOMANYREFS:    return -LINUX_ETOOMANYREFS;
        case ETIMEDOUT:       return -LINUX_ETIMEDOUT;
        case ECONNREFUSED:    return -LINUX_ECONNREFUSED;
        case EHOSTDOWN:       return -LINUX_EHOSTDOWN;
        case EHOSTUNREACH:    return -LINUX_EHOSTUNREACH;
        case EALREADY:        return -LINUX_EALREADY;
        case EINPROGRESS:     return -LINUX_EINPROGRESS;
#ifdef ENOATTR
        case ENOATTR:         return -LINUX_ENODATA;
#endif
        default:              return -LINUX_EINVAL;
    }
}

#endif /* LTM_H */
