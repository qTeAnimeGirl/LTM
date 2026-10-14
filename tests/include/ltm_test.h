#ifndef LTM_TEST_H
#define LTM_TEST_H

/*
 * LTM Test Support Library (Freestanding Linux ARM64)
 * For use in testing the LTM (Linux To Mac) translation runtime.
 */

typedef unsigned long uint64_t;
typedef long int64_t;
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned char uint8_t;
typedef signed char int8_t;
typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;
typedef unsigned long uintptr_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef enum { false = 0, true = 1 } bool;

/* Linux ARM64 System Call Numbers */
#define SYS_getcwd          17
#define SYS_dup             23
#define SYS_dup3            24
#define SYS_flock           32
#define SYS_mkdirat         34
#define SYS_unlinkat        35
#define SYS_symlinkat       36
#define SYS_linkat          37
#define SYS_renameat        38
#define SYS_statfs          43
#define SYS_fstatfs         44
#define SYS_truncate        45
#define SYS_ftruncate       46
#define SYS_fallocate       47
#define SYS_faccessat       48
#define SYS_chdir           49
#define SYS_fchdir          50
#define SYS_chroot          51
#define SYS_fchmod          52
#define SYS_fchmodat        53
#define SYS_fchownat        54
#define SYS_fchown          55
#define SYS_openat          56
#define SYS_close           57
#define SYS_pipe2           59
#define SYS_getdents64      61
#define SYS_lseek           62
#define SYS_read            63
#define SYS_write           64
#define SYS_pread64         67
#define SYS_pwrite64        68
#define SYS_signalfd4       74
#define SYS_readlinkat      78
#define SYS_newfstatat      79
#define SYS_fstat           80
#define SYS_sync            81
#define SYS_fsync           82
#define SYS_fdatasync       83
#define SYS_timerfd_create  85
#define SYS_timerfd_settime 86
#define SYS_timerfd_gettime 87
#define SYS_exit            93
#define SYS_exit_group      94
#define SYS_clock_nanosleep 115
#define SYS_sched_setaffinity 122
#define SYS_sched_getaffinity 123
#define SYS_sched_yield     124
#define SYS_kill            129
#define SYS_tkill           130
#define SYS_tgkill          131
#define SYS_times           153
#define SYS_setpgid         154
#define SYS_getpgid         155
#define SYS_getsid          156
#define SYS_setsid          157
#define SYS_uname           160
#define SYS_getrusage       165
#define SYS_umask           166
#define SYS_getpid          172
#define SYS_getppid         173
#define SYS_getuid          174
#define SYS_geteuid         175
#define SYS_getgid          176
#define SYS_getegid         177
#define SYS_sysinfo         179
#define SYS_socket          198
#define SYS_socketpair      199
#define SYS_bind            200
#define SYS_listen          201
#define SYS_accept          202
#define SYS_connect         203
#define SYS_readahead       213
#define SYS_brk             214
#define SYS_munmap          215
#define SYS_mmap            222
#define SYS_madvise         233
#define SYS_accept4         242
#define SYS_syncfs          267
#define SYS_renameat2       276
#define SYS_getrandom       278
#define SYS_memfd_create    279
#define SYS_copy_file_range 285
#define SYS_close_range     436
#define SYS_openat2         437
#define SYS_faccessat2      439

/* File open flags */
#define AT_FDCWD        (-100)
#define O_RDONLY        00000000
#define O_WRONLY        00000001
#define O_RDWR          00000002
#define O_CREAT         00000100
#define O_EXCL          00000200
#define O_TRUNC         00001000
#define O_APPEND        00002000
#define O_DIRECTORY     00040000

#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_REMOVEDIR        0x200
#define AT_EMPTY_PATH       0x1000

/* Seek flags */
#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

/* Memory protection & map flags */
#define PROT_NONE       0x0
#define PROT_READ       0x1
#define PROT_WRITE      0x2
#define PROT_EXEC       0x4

#define MAP_SHARED      0x01
#define MAP_PRIVATE     0x02
#define MAP_FIXED       0x10
#define MAP_ANONYMOUS   0x20
#define MAP_FAILED      ((void *)-1)

/* Common Linux Errno values */
#define EPERM           1
#define ENOENT          2
#define ESRCH           3
#define EBADF           9
#define EAGAIN          11
#define EWOULDBLOCK     11
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EEXIST          17
#define ENODEV          19
#define ENOTDIR         20
#define EISDIR          21
#define EINVAL          22
#define EFBIG           27
#define ESPIPE          29
#define ENOSYS          38
#define ENOTSOCK        88

/* Flock Operations */
#define LOCK_SH         1
#define LOCK_EX         2
#define LOCK_NB         4
#define LOCK_UN         8

/* Fallocate Flags */
#define FALLOC_FL_KEEP_SIZE     0x01
#define FALLOC_FL_PUNCH_HOLE    0x02

/* Signalfd / Timerfd / Socket Flags */
#define SFD_CLOEXEC     02000000
#define SFD_NONBLOCK    00004000
#define TFD_CLOEXEC     02000000
#define TFD_NONBLOCK    00004000
#define TFD_TIMER_ABSTIME 1
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#define SOCK_CLOEXEC    02000000
#define SOCK_NONBLOCK   00004000

/* Memfd Flags */
#define MFD_CLOEXEC         0x0001U
#define MFD_ALLOW_SEALING   0x0002U

/* Getrandom Flags */
#define GRND_NONBLOCK       0x0001
#define GRND_RANDOM         0x0002
#define GRND_INSECURE       0x0004

/* Close Range Flags */
#define CLOSE_RANGE_UNSHARE (1U << 1)
#define CLOSE_RANGE_CLOEXEC (1U << 2)

/* Openat2 Flags and Struct */
#define RESOLVE_NO_XDEV         0x01
#define RESOLVE_NO_MAGICLINKS   0x02
#define RESOLVE_NO_SYMLINKS     0x04
#define RESOLVE_BENEATH         0x08
#define RESOLVE_IN_ROOT         0x10
#define RESOLVE_CACHED          0x20

struct linux_open_how {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
};

/* Faccessat2 Flags */
#define AT_SYMLINK_NOFOLLOW 0x100
#define AT_EACCESS          0x200
#ifndef F_OK
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
#endif

/* Timespec & Itimerspec */
struct linux_timespec {
    long tv_sec;
    long tv_nsec;
};

struct linux_itimerspec {
    struct linux_timespec it_interval;
    struct linux_timespec it_value;
};

/* Madvise Advice */
#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4

/* Getrusage Who */
#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD   1

/* Linux Stat 64 (128 bytes) */
struct linux_stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t  st_size;
    int32_t  st_blksize;
    int32_t  __pad2;
    int64_t  st_blocks;
    int64_t  st_atime_sec;
    uint64_t st_atime_nsec;
    int64_t  st_mtime_sec;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime_sec;
    uint64_t st_ctime_nsec;
    int32_t  __pad3[2];
};

/* Linux signalfd_siginfo (128 bytes) */
struct linux_signalfd_siginfo {
    uint32_t ssi_signo;
    int32_t  ssi_errno;
    int32_t  ssi_code;
    uint32_t ssi_pid;
    uint32_t ssi_uid;
    int32_t  ssi_fd;
    uint32_t ssi_tid;
    uint32_t ssi_band;
    uint32_t ssi_overrun;
    uint32_t ssi_trapno;
    int32_t  ssi_status;
    int32_t  ssi_int;
    uint64_t ssi_ptr;
    uint64_t ssi_utime;
    uint64_t ssi_stime;
    uint64_t ssi_addr;
    uint16_t ssi_addr_lsb;
    uint16_t __pad2;
    int32_t  ssi_syscall;
    uint64_t ssi_call_addr;
    uint32_t ssi_arch;
    uint8_t  __pad[28];
};

/* Linux sysinfo structure (112 bytes) */
struct linux_sysinfo {
    int64_t  uptime;
    uint64_t loads[3];
    uint64_t totalram;
    uint64_t freeram;
    uint64_t sharedram;
    uint64_t bufferram;
    uint64_t totalswap;
    uint64_t freeswap;
    uint16_t procs;
    uint16_t pad;
    uint32_t __pad;
    uint64_t totalhigh;
    uint64_t freehigh;
    uint32_t mem_unit;
    char     _f[20 - 2 * sizeof(uint64_t) - sizeof(uint32_t)];
};

/* Linux utsname structure (390 bytes) */
struct linux_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

/* Directory entry 64 */
struct linux_dirent64 {
    uint64_t        d_ino;
    int64_t         d_off;
    unsigned short  d_reclen;
    unsigned char   d_type;
    char            d_name[];
};

#define DT_UNKNOWN      0
#define DT_FIFO         1
#define DT_CHR          2
#define DT_DIR          4
#define DT_BLK          6
#define DT_REG          8
#define DT_LNK          10
#define DT_SOCK         12
#define DT_WHT          14

/* Statfs 64 */
struct linux_statfs {
    int64_t  f_type;
    int64_t  f_bsize;
    int64_t  f_blocks;
    int64_t  f_bfree;
    int64_t  f_bavail;
    int64_t  f_files;
    int64_t  f_ffree;
    struct { int32_t val[2]; } f_fsid;
    int64_t  f_namelen;
    int64_t  f_frsize;
    int64_t  f_flags;
    int64_t  f_spare[4];
};

/* Times struct */
struct linux_tms {
    long tms_utime;
    long tms_stime;
    long tms_cutime;
    long tms_cstime;
};

/* Timeval struct */
struct linux_timeval {
    long tv_sec;
    long tv_usec;
};

/* Rusage struct */
struct linux_rusage {
    struct linux_timeval ru_utime;
    struct linux_timeval ru_stime;
    long   ru_maxrss;
    long   ru_ixrss;
    long   ru_idrss;
    long   ru_isrss;
    long   ru_minflt;
    long   ru_majflt;
    long   ru_nswap;
    long   ru_inblock;
    long   ru_oublock;
    long   ru_msgsnd;
    long   ru_msgrcv;
    long   ru_nsignals;
    long   ru_nvcsw;
    long   ru_nivcsw;
};

/* Linux Auxiliary Vector Types */
#define AT_NULL         0
#define AT_IGNORE       1
#define AT_EXECFD       2
#define AT_PHDR         3
#define AT_PHENT        4
#define AT_PHNUM        5
#define AT_PAGESZ       6
#define AT_BASE         7
#define AT_FLAGS        8
#define AT_ENTRY        9
#define AT_NOTELF       10
#define AT_UID          11
#define AT_EUID         12
#define AT_GID          13
#define AT_EGID         14
#define AT_PLATFORM     15
#define AT_HWCAP        16
#define AT_CLKTCK       17
#define AT_SECURE       23
#define AT_BASE_PLATFORM 24
#define AT_RANDOM       25
#define AT_HWCAP2       26
#define AT_EXECFN       31

typedef struct {
    uint64_t a_type;
    uint64_t a_val;
} Elf64_auxv_t;

/* Syscall execution primitives for ARM64 */
static inline long sys_syscall0(long n) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline long sys_syscall1(long n, long a1) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a1;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "0"(x0) : "memory");
    return x0;
}

static inline long sys_syscall2(long n, long a1, long a2) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "0"(x0), "r"(x1) : "memory");
    return x0;
}

static inline long sys_syscall3(long n, long a1, long a2, long a3) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "0"(x0), "r"(x1), "r"(x2) : "memory");
    return x0;
}

static inline long sys_syscall4(long n, long a1, long a2, long a3, long a4) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    register long x3 __asm__("x3") = a4;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}

static inline long sys_syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    register long x3 __asm__("x3") = a4;
    register long x4 __asm__("x4") = a5;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4) : "memory");
    return x0;
}

static inline long sys_syscall6(long n, long a1, long a2, long a3, long a4, long a5, long a6) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a1;
    register long x1 __asm__("x1") = a2;
    register long x2 __asm__("x2") = a3;
    register long x3 __asm__("x3") = a4;
    register long x4 __asm__("x4") = a5;
    register long x5 __asm__("x5") = a6;
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8), "0"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;
}

/* Syscall helper wrappers */
static inline void sys_exit(int code) {
    sys_syscall1(SYS_exit, code);
    while (1) {}
}

static inline void sys_exit_group(int code) {
    sys_syscall1(SYS_exit_group, code);
    while (1) {}
}

static inline ssize_t sys_write(int fd, const void *buf, size_t count) {
    return (ssize_t)sys_syscall3(SYS_write, fd, (long)buf, count);
}

static inline ssize_t sys_read(int fd, void *buf, size_t count) {
    return (ssize_t)sys_syscall3(SYS_read, fd, (long)buf, count);
}

static inline int sys_openat(int dirfd, const char *pathname, int flags, int mode) {
    return (int)sys_syscall4(SYS_openat, dirfd, (long)pathname, flags, mode);
}

static inline int sys_close(int fd) {
    return (int)sys_syscall1(SYS_close, fd);
}

static inline off_t sys_lseek(int fd, off_t offset, int whence) {
    return (off_t)sys_syscall3(SYS_lseek, fd, offset, whence);
}

static inline void *sys_brk(void *addr) {
    return (void *)sys_syscall1(SYS_brk, (long)addr);
}

static inline void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return (void *)sys_syscall6(SYS_mmap, (long)addr, length, prot, flags, fd, offset);
}

static inline int sys_munmap(void *addr, size_t length) {
    return (int)sys_syscall2(SYS_munmap, (long)addr, length);
}

static inline int sys_statfs(const char *path, struct linux_statfs *buf) {
    return (int)sys_syscall2(SYS_statfs, (long)path, (long)buf);
}

static inline int sys_fstatfs(int fd, struct linux_statfs *buf) {
    return (int)sys_syscall2(SYS_fstatfs, fd, (long)buf);
}

static inline int sys_truncate(const char *path, off_t length) {
    return (int)sys_syscall2(SYS_truncate, (long)path, length);
}

static inline int sys_ftruncate(int fd, off_t length) {
    return (int)sys_syscall2(SYS_ftruncate, fd, length);
}

static inline int sys_fchmod(int fd, int mode) {
    return (int)sys_syscall2(SYS_fchmod, fd, mode);
}

static inline int sys_fchmodat(int dirfd, const char *pathname, int mode, int flags) {
    return (int)sys_syscall4(SYS_fchmodat, dirfd, (long)pathname, mode, flags);
}

static inline int sys_fchown(int fd, int uid, int gid) {
    return (int)sys_syscall3(SYS_fchown, fd, uid, gid);
}

static inline int sys_fchownat(int dirfd, const char *pathname, int uid, int gid, int flags) {
    return (int)sys_syscall5(SYS_fchownat, dirfd, (long)pathname, uid, gid, flags);
}

static inline int sys_fsync(int fd) {
    return (int)sys_syscall1(SYS_fsync, fd);
}

static inline int sys_fdatasync(int fd) {
    return (int)sys_syscall1(SYS_fdatasync, fd);
}

static inline int sys_sched_setaffinity(int pid, size_t cpusetsize, const unsigned long *mask) {
    return (int)sys_syscall3(SYS_sched_setaffinity, pid, cpusetsize, (long)mask);
}

static inline int sys_sched_getaffinity(int pid, size_t cpusetsize, unsigned long *mask) {
    return (int)sys_syscall3(SYS_sched_getaffinity, pid, cpusetsize, (long)mask);
}

static inline int sys_sched_yield(void) {
    return (int)sys_syscall0(SYS_sched_yield);
}

static inline long sys_times(struct linux_tms *buf) {
    return sys_syscall1(SYS_times, (long)buf);
}

static inline int sys_setpgid(int pid, int pgid) {
    return (int)sys_syscall2(SYS_setpgid, pid, pgid);
}

static inline int sys_getpgid(int pid) {
    return (int)sys_syscall1(SYS_getpgid, pid);
}

static inline int sys_getsid(int pid) {
    return (int)sys_syscall1(SYS_getsid, pid);
}

static inline int sys_setsid(void) {
    return (int)sys_syscall0(SYS_setsid);
}

static inline int sys_getrusage(int who, struct linux_rusage *usage) {
    return (int)sys_syscall2(SYS_getrusage, who, (long)usage);
}

static inline int sys_umask(int mask) {
    return (int)sys_syscall1(SYS_umask, mask);
}

static inline ssize_t sys_readahead(int fd, off_t offset, size_t count) {
    return (ssize_t)sys_syscall3(SYS_readahead, fd, offset, count);
}

static inline int sys_madvise(void *addr, size_t length, int advice) {
    return (int)sys_syscall3(SYS_madvise, (long)addr, length, advice);
}

static inline int sys_getdents64(int fd, void *dirp, size_t count) {
    return (int)sys_syscall3(SYS_getdents64, fd, (long)dirp, count);
}

static inline int sys_getpid(void) {
    return (int)sys_syscall0(SYS_getpid);
}

static inline int sys_flock(int fd, int operation) {
    return (int)sys_syscall2(SYS_flock, fd, operation);
}

static inline int sys_fallocate(int fd, int mode, off_t offset, off_t len) {
    return (int)sys_syscall4(SYS_fallocate, fd, mode, offset, len);
}

static inline int sys_chroot(const char *path) {
    return (int)sys_syscall1(SYS_chroot, (long)path);
}

static inline ssize_t sys_pread64(int fd, void *buf, size_t count, off_t offset) {
    return (ssize_t)sys_syscall4(SYS_pread64, fd, (long)buf, count, offset);
}

static inline ssize_t sys_pwrite64(int fd, const void *buf, size_t count, off_t offset) {
    return (ssize_t)sys_syscall4(SYS_pwrite64, fd, (long)buf, count, offset);
}

static inline int sys_signalfd4(int ufd, const void *mask, size_t sizemask, int flags) {
    return (int)sys_syscall4(SYS_signalfd4, ufd, (long)mask, sizemask, flags);
}

static inline void sys_sync(void) {
    sys_syscall0(SYS_sync);
}

static inline int sys_timerfd_create(int clockid, int flags) {
    return (int)sys_syscall2(SYS_timerfd_create, clockid, flags);
}

static inline int sys_timerfd_settime(int fd, int flags, const struct linux_itimerspec *new_val, struct linux_itimerspec *old_val) {
    return (int)sys_syscall4(SYS_timerfd_settime, fd, flags, (long)new_val, (long)old_val);
}

static inline int sys_timerfd_gettime(int fd, struct linux_itimerspec *curr_val) {
    return (int)sys_syscall2(SYS_timerfd_gettime, fd, (long)curr_val);
}

static inline int sys_clock_nanosleep(int clockid, int flags, const struct linux_timespec *req, struct linux_timespec *rem) {
    return (int)sys_syscall4(SYS_clock_nanosleep, clockid, flags, (long)req, (long)rem);
}

static inline int sys_accept4(int sockfd, void *addr, void *addrlen, int flags) {
    return (int)sys_syscall4(SYS_accept4, sockfd, (long)addr, (long)addrlen, flags);
}

static inline int sys_syncfs(int fd) {
    return (int)sys_syscall1(SYS_syncfs, fd);
}

static inline int sys_memfd_create(const char *name, unsigned int flags) {
    return (int)sys_syscall2(SYS_memfd_create, (long)name, flags);
}

static inline ssize_t sys_getrandom(void *buf, size_t buflen, unsigned int flags) {
    return (ssize_t)sys_syscall3(SYS_getrandom, (long)buf, buflen, flags);
}

static inline ssize_t sys_copy_file_range(int fd_in, off_t *off_in, int fd_out, off_t *off_out, size_t len, unsigned int flags) {
    return (ssize_t)sys_syscall6(SYS_copy_file_range, fd_in, (long)off_in, fd_out, (long)off_out, len, flags);
}

static inline int sys_close_range(unsigned int first, unsigned int last, unsigned int flags) {
    return (int)sys_syscall3(SYS_close_range, first, last, flags);
}

static inline int sys_openat2(int dirfd, const char *pathname, struct linux_open_how *how, size_t size) {
    return (int)sys_syscall4(SYS_openat2, dirfd, (long)pathname, (long)how, size);
}

static inline int sys_faccessat2(int dirfd, const char *pathname, int mode, int flags) {
    return (int)sys_syscall4(SYS_faccessat2, dirfd, (long)pathname, mode, flags);
}

static inline int sys_getcwd(char *buf, size_t size) {
    return (int)sys_syscall2(SYS_getcwd, (long)buf, size);
}

static inline int sys_dup(int oldfd) {
    return (int)sys_syscall1(SYS_dup, oldfd);
}

static inline int sys_dup3(int oldfd, int newfd, int flags) {
    return (int)sys_syscall3(SYS_dup3, oldfd, newfd, flags);
}

static inline int sys_mkdirat(int dirfd, const char *pathname, int mode) {
    return (int)sys_syscall3(SYS_mkdirat, dirfd, (long)pathname, mode);
}

static inline int sys_unlinkat(int dirfd, const char *pathname, int flags) {
    return (int)sys_syscall3(SYS_unlinkat, dirfd, (long)pathname, flags);
}

static inline int sys_symlinkat(const char *target, int newdirfd, const char *linkpath) {
    return (int)sys_syscall3(SYS_symlinkat, (long)target, newdirfd, (long)linkpath);
}

static inline int sys_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *linkpath, int flags) {
    return (int)sys_syscall5(SYS_linkat, olddirfd, (long)oldpath, newdirfd, (long)linkpath, flags);
}

static inline int sys_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    return (int)sys_syscall4(SYS_renameat, olddirfd, (long)oldpath, newdirfd, (long)newpath);
}

static inline int sys_renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags) {
    return (int)sys_syscall5(SYS_renameat2, olddirfd, (long)oldpath, newdirfd, (long)newpath, flags);
}

static inline int sys_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    return (int)sys_syscall4(SYS_faccessat, dirfd, (long)pathname, mode, flags);
}

static inline int sys_chdir(const char *path) {
    return (int)sys_syscall1(SYS_chdir, (long)path);
}

static inline int sys_fchdir(int fd) {
    return (int)sys_syscall1(SYS_fchdir, fd);
}

static inline int sys_pipe2(int pipefd[2], int flags) {
    return (int)sys_syscall2(SYS_pipe2, (long)pipefd, flags);
}

static inline int sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    return (int)sys_syscall4(SYS_readlinkat, dirfd, (long)pathname, (long)buf, bufsiz);
}

static inline int sys_fstat(int fd, struct linux_stat *statbuf) {
    return (int)sys_syscall2(SYS_fstat, fd, (long)statbuf);
}

static inline int sys_newfstatat(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags) {
    return (int)sys_syscall4(SYS_newfstatat, dirfd, (long)pathname, (long)statbuf, flags);
}

static inline int sys_kill(int pid, int sig) {
    return (int)sys_syscall2(SYS_kill, pid, sig);
}

static inline int sys_tkill(int tid, int sig) {
    return (int)sys_syscall2(SYS_tkill, tid, sig);
}

static inline int sys_tgkill(int tgid, int tid, int sig) {
    return (int)sys_syscall3(SYS_tgkill, tgid, tid, sig);
}

static inline int sys_uname(struct linux_utsname *buf) {
    return (int)sys_syscall1(SYS_uname, (long)buf);
}

static inline int sys_sysinfo(struct linux_sysinfo *info) {
    return (int)sys_syscall1(SYS_sysinfo, (long)info);
}

static inline int sys_getppid(void) {
    return (int)sys_syscall0(SYS_getppid);
}

static inline int sys_getuid(void) {
    return (int)sys_syscall0(SYS_getuid);
}

static inline int sys_geteuid(void) {
    return (int)sys_syscall0(SYS_geteuid);
}

static inline int sys_getgid(void) {
    return (int)sys_syscall0(SYS_getgid);
}

static inline int sys_getegid(void) {
    return (int)sys_syscall0(SYS_getegid);
}

static inline int sys_socket(int domain, int type, int protocol) {
    return (int)sys_syscall3(SYS_socket, domain, type, protocol);
}

static inline int sys_socketpair(int domain, int type, int protocol, int sv[2]) {
    return (int)sys_syscall4(SYS_socketpair, domain, type, protocol, (long)sv);
}

static inline int sys_bind(int fd, const void *addr, unsigned int len) {
    return (int)sys_syscall3(SYS_bind, fd, (long)addr, len);
}

static inline int sys_listen(int fd, int backlog) {
    return (int)sys_syscall2(SYS_listen, fd, backlog);
}

static inline int sys_accept(int fd, void *addr, void *len) {
    return (int)sys_syscall3(SYS_accept, fd, (long)addr, (long)len);
}

static inline int sys_connect(int fd, const void *addr, unsigned int len) {
    return (int)sys_syscall3(SYS_connect, fd, (long)addr, len);
}

/* String / memory utilities */
static inline size_t ltm_strlen(const char *s) {
    size_t len = 0;
    if (!s) return 0;
    while (*s++) len++;
    return len;
}

static inline int ltm_strcmp(const char *s1, const char *s2) {
    if (!s1 && !s2) return 0;
    if (!s1) return -1;
    if (!s2) return 1;
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static inline int ltm_strncmp(const char *s1, const char *s2, size_t n) {
    if (n == 0) return 0;
    while (n-- > 0 && *s1 && (*s1 == *s2)) {
        if (n == 0 || *s1 == '\0') return 0;
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

static inline const char *ltm_strstr(const char *haystack, const char *needle) {
    size_t needle_len;
    if (!haystack || !needle) return NULL;
    needle_len = ltm_strlen(needle);
    if (needle_len == 0) return haystack;
    while (*haystack) {
        if (*haystack == *needle) {
            if (ltm_strncmp(haystack, needle, needle_len) == 0) {
                return haystack;
            }
        }
        haystack++;
    }
    return NULL;
}

static inline void *ltm_memset(void *s, int c, size_t n) {
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

static inline void *ltm_memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

static inline int ltm_memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

/* Printing utilities */
static inline void ltm_print(const char *s) {
    if (s) {
        sys_write(1, s, ltm_strlen(s));
    }
}

static inline void ltm_print_err(const char *s) {
    if (s) {
        sys_write(2, s, ltm_strlen(s));
    }
}

static inline void ltm_print_int(long val) {
    char buf[32];
    int i = 0;
    bool neg = false;
    if (val == 0) {
        ltm_print("0");
        return;
    }
    if (val < 0) {
        neg = true;
        val = -val;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    if (neg) buf[i++] = '-';
    // reverse
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = '\0';
    ltm_print(buf);
}

static inline void ltm_print_hex(unsigned long val) {
    char buf[20];
    const char hex[] = "0123456789abcdef";
    int i = 0;
    ltm_print("0x");
    if (val == 0) {
        ltm_print("0");
        return;
    }
    while (val > 0) {
        buf[i++] = hex[val & 0xf];
        val >>= 4;
    }
    for (int j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }
    buf[i] = '\0';
    ltm_print(buf);
}

/* Testing assertion macros */
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            ltm_print_err("[FAIL] Assertion failed: " msg "\n"); \
            sys_exit(1); \
        } \
    } while (0)

#define TEST_PASS(msg) \
    do { \
        ltm_print("[PASS] " msg "\n"); \
    } while (0)

/* Test entrypoint harness */
int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv);

__attribute__((used)) void _c_entry(uint64_t *sp) {
    uint64_t argc = sp[0];
    char **argv = (char **)&sp[1];
    char **envp = &argv[argc + 1];
    char **p = envp;
    while (*p != 0) {
        p++;
    }
    Elf64_auxv_t *auxv = (Elf64_auxv_t *)(p + 1);
    int ret = ltm_test_main((int)argc, argv, envp, auxv);
    sys_exit(ret);
}

#define LTM_TEST_ENTRYPOINT \
    __attribute__((naked)) void _start(void) { \
        __asm__ volatile( \
            "mov x0, sp\n" \
            "bl _c_entry\n" \
            "mov x8, #93\n" \
            "svc #0\n" \
        ); \
    }

#endif /* LTM_TEST_H */
