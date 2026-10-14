#ifndef SYSCALLS_H
#define SYSCALLS_H

#include "ltm.h"

/* Linux ARM64 stat structure (128 bytes) */
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

/* Linux 64-bit dirent structure */
struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
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

/* Linux iovec */
struct linux_iovec {
    uint64_t iov_base;
    uint64_t iov_len;
};

/* Linux timespec */
struct linux_timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

/* Linux itimerspec */
struct linux_itimerspec {
    struct linux_timespec it_interval;
    struct linux_timespec it_value;
};

/* Linux open_how (openat2) */
struct linux_open_how {
    uint64_t flags;
    uint64_t mode;
    uint64_t resolve;
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

/* Linux timeval */
struct linux_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

/* Linux timezone */
struct linux_timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

/* Linux rlimit64 */
struct linux_rlimit64 {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

/* Linux rlimit */
struct linux_rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

/* Linux 128-byte siginfo */
struct linux_siginfo {
    int32_t si_signo;
    int32_t si_errno;
    int32_t si_code;
    int32_t __pad0;
    union {
        struct {
            int32_t _pid;
            uint32_t _uid;
            int32_t _status;
            int32_t __pad1;
        } _sigchld;
        uint8_t _pad[112];
    } _sifields;
};

/* Linux sched_param */
struct linux_sched_param {
    int32_t sched_priority;
};

/* Linux winsize */
struct linux_winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

/* Linux ARM64 termios (36 bytes) */
struct linux_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
};

/* Linux Statfs 64 (120 bytes) */
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

/* Linux Times */
struct linux_tms {
    long tms_utime;
    long tms_stime;
    long tms_cutime;
    long tms_cstime;
};

/* Linux Rusage */
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

/* Syscall Dispatcher Registration & Entry */
int syscall_dispatcher_init(void);
int64_t syscall_dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5, void *state);

/* Individual Syscall Handlers */
int64_t handle_read(int fd, void *buf, size_t count);
int64_t handle_write(int fd, const void *buf, size_t count);
int64_t handle_openat(int dirfd, const char *pathname, int flags, mode_t mode);
int64_t handle_close(int fd);
int64_t handle_fstat(int fd, struct linux_stat *statbuf);
int64_t handle_newfstatat(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags);
int64_t handle_lseek(int fd, off_t offset, int whence);
int64_t handle_ioctl(int fd, unsigned long request, void *arg);
int64_t handle_readv(int fd, const struct linux_iovec *iov, int iovcnt);
int64_t handle_writev(int fd, const struct linux_iovec *iov, int iovcnt);
int64_t handle_dup(int oldfd);
int64_t handle_dup3(int oldfd, int newfd, int flags);
int64_t handle_fcntl(int fd, int cmd, unsigned long arg);
int64_t handle_getdents64(int fd, void *dirp, size_t count);
int64_t handle_ppoll(struct pollfd *fds, nfds_t nfds, const struct linux_timespec *tmo_p, const void *sigmask);
int64_t handle_pselect6(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                        const struct linux_timespec *timeout, const void *sig);

int64_t handle_tkill(int tid, int sig);
int64_t handle_tgkill(int tgid, int tid, int sig);
int64_t handle_sigaltstack(const void *ss, void *oss);

struct linux_sigaction {
    void (*l_handler)(int);
    uint64_t l_flags;
    void (*l_restorer)(void);
    uint64_t l_mask;
};

void ltm_sigreturn_stub(void);
int64_t handle_rt_sigaction(int signum, const struct linux_sigaction *act, struct linux_sigaction *oldact, size_t sigsetsize);
int64_t handle_rt_sigreturn(void *state);
int64_t handle_rt_sigprocmask(int how, const uint64_t *set, uint64_t *oldset, size_t sigsetsize);

int64_t handle_brk(uintptr_t brk_addr);
int64_t handle_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int64_t handle_munmap(void *addr, size_t length);
int64_t handle_mprotect(void *addr, size_t length, int prot);

int64_t handle_getpid(void);
int64_t handle_getppid(void);
int64_t handle_getuid(void);
int64_t handle_geteuid(void);
int64_t handle_getgid(void);
int64_t handle_getegid(void);
int64_t handle_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid);
int64_t handle_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid);
int64_t handle_getgroups(int size, gid_t list[]);
int64_t handle_setgroups(size_t size, const gid_t *list);
int64_t handle_capget(void *hdr, void *data);
int64_t handle_capset(void *hdr, const void *data);
int64_t handle_gettid(void);
int64_t handle_uname(struct linux_utsname *buf);
int64_t handle_sysinfo(struct linux_sysinfo *info);

int64_t handle_clock_gettime(int clock_id, struct linux_timespec *tp);
int64_t handle_gettimeofday(struct linux_timeval *tv, struct linux_timezone *tz);
int64_t handle_nanosleep(const struct linux_timespec *req, struct linux_timespec *rem);

int64_t handle_exit(int status);
int64_t handle_exit_group(int status);

int64_t handle_set_tid_address(int *tidptr);
int64_t handle_futex(uint32_t *uaddr, int op, uint32_t val,
                     const struct linux_timespec *timeout, uint32_t *uaddr2, uint32_t val3);
int64_t handle_clone(uint64_t flags, void *stack, int *ptid, void *tls, int *ctid, void *state);

int64_t handle_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz);
int64_t handle_prlimit64(int pid, int resource, const struct linux_rlimit64 *new_limit, struct linux_rlimit64 *old_limit);
int64_t handle_getrandom(void *buf, size_t buflen, unsigned int flags);
int64_t handle_wait4(pid_t pid, int *status, int options, void *rusage);
int64_t handle_pipe2(int pipefd[2], int flags);
int64_t handle_execve(const char *pathname, char *const argv[], char *const envp[]);
int64_t handle_getcwd(char *buf, size_t size);
int64_t handle_chdir(const char *path);
int64_t handle_fchdir(int fd);
int64_t handle_setpgid(pid_t pid, pid_t pgid);
int64_t handle_getpgid(pid_t pid);

/* Extended Filesystem Operations */
int64_t handle_unlinkat(int dirfd, const char *pathname, int flags);
int64_t handle_mkdirat(int dirfd, const char *pathname, mode_t mode);
int64_t handle_faccessat(int dirfd, const char *pathname, int mode, int flags);
int64_t handle_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath);
int64_t handle_renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags);
int64_t handle_symlinkat(const char *target, int newdirfd, const char *linkpath);
int64_t handle_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *linkpath, int flags);
int64_t handle_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags);
int64_t handle_fchmod(int fd, mode_t mode);
int64_t handle_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags);
int64_t handle_fchown(int fd, uid_t owner, gid_t group);
int64_t handle_utimensat(int dirfd, const char *pathname, const struct linux_timespec times[2], int flags);

/* Scheduler & Affinity */
int64_t handle_sched_getaffinity(pid_t pid, size_t cpusetsize, unsigned long *mask);
int64_t handle_sched_setaffinity(pid_t pid, size_t cpusetsize, const unsigned long *mask);

/* Networking & Sockets */
int64_t handle_socket(int domain, int type, int protocol);
int64_t handle_socketpair(int domain, int type, int protocol, int sv[2]);
int64_t handle_connect(int fd, const void *addr, socklen_t len);
int64_t handle_bind(int fd, const void *addr, socklen_t len);
int64_t handle_listen(int fd, int backlog);
int64_t handle_accept(int fd, void *addr, socklen_t *len);
int64_t handle_accept4(int fd, void *addr, socklen_t *len, int flags);
int64_t handle_getsockname(int fd, void *addr, socklen_t *len);
int64_t handle_getpeername(int fd, void *addr, socklen_t *len);
int64_t handle_sendto(int fd, const void *buf, size_t len, int flags, const void *dest_addr, socklen_t addrlen);
int64_t handle_recvfrom(int fd, void *buf, size_t len, int flags, void *src_addr, socklen_t *addrlen);
int64_t handle_sendmsg(int fd, const void *msg, int flags);
int64_t handle_recvmsg(int fd, void *msg, int flags);
int64_t handle_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen);
int64_t handle_getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen);
int64_t handle_shutdown(int fd, int how);

/* Timers & Events */
int64_t handle_setitimer(int which, const void *new_val, void *old_val);
int64_t handle_getitimer(int which, void *curr_val);
int64_t handle_epoll_create1(int flags);
int64_t handle_epoll_ctl(int epfd, int op, int fd, void *event);
int64_t handle_epoll_pwait(int epfd, void *events, int maxevents, int timeout, const void *sigmask);

/* Wave 1 Syscalls */
int64_t handle_statfs(const char *pathname, struct linux_statfs *buf);
int64_t handle_fstatfs(int fd, struct linux_statfs *buf);
int64_t handle_truncate(const char *path, off_t length);
int64_t handle_ftruncate(int fd, off_t length);
int64_t handle_fsync(int fd);
int64_t handle_fdatasync(int fd);
int64_t handle_sched_yield(void);
int64_t handle_times(struct linux_tms *buf);
int64_t handle_getsid(pid_t pid);
int64_t handle_setsid(void);
int64_t handle_getrusage(int who, struct linux_rusage *usage);
int64_t handle_umask(mode_t mask);
int64_t handle_readahead(int fd, off_t offset, size_t count);
int64_t handle_madvise(void *addr, size_t length, int advice);

/* Wave 2 Syscalls */
int64_t handle_flock(int fd, int operation);
int64_t handle_fallocate(int fd, int mode, off_t offset, off_t len);
int64_t handle_chroot(const char *path);
int64_t handle_pread64(int fd, void *buf, size_t count, off_t offset);
int64_t handle_pwrite64(int fd, const void *buf, size_t count, off_t offset);
int64_t handle_signalfd4(int ufd, const uint64_t *mask, size_t sizemask, int flags);
int64_t handle_sync(void);
int64_t handle_syncfs(int fd);
int64_t handle_timerfd_create(int clockid, int flags);
int64_t handle_timerfd_settime(int fd, int flags, const struct linux_itimerspec *new_value, struct linux_itimerspec *old_value);
int64_t handle_timerfd_gettime(int fd, struct linux_itimerspec *curr_value);
int64_t handle_clock_nanosleep(int clockid, int flags, const struct linux_timespec *req, struct linux_timespec *rem);
int64_t handle_memfd_create(const char *name, unsigned int flags);
int64_t handle_copy_file_range(int fd_in, off_t *off_in, int fd_out, off_t *off_out, size_t len, unsigned int flags);
int64_t handle_close_range(unsigned int first, unsigned int last, unsigned int flags);
int64_t handle_openat2(int dirfd, const char *pathname, struct linux_open_how *how, size_t size);
int64_t handle_faccessat2(int dirfd, const char *pathname, int mode, int flags);
int64_t handle_kill(pid_t pid, int sig);

/* Wave 3 Syscalls */
/* Extended Attributes (xattr) */
int64_t handle_setxattr(const char *path, const char *name, const void *value, size_t size, int flags);
int64_t handle_lsetxattr(const char *path, const char *name, const void *value, size_t size, int flags);
int64_t handle_fsetxattr(int fd, const char *name, const void *value, size_t size, int flags);
int64_t handle_getxattr(const char *path, const char *name, void *value, size_t size);
int64_t handle_lgetxattr(const char *path, const char *name, void *value, size_t size);
int64_t handle_fgetxattr(int fd, const char *name, void *value, size_t size);
int64_t handle_listxattr(const char *path, char *list, size_t size);
int64_t handle_llistxattr(const char *path, char *list, size_t size);
int64_t handle_flistxattr(int fd, char *list, size_t size);
int64_t handle_removexattr(const char *path, const char *name);
int64_t handle_lremovexattr(const char *path, const char *name);
int64_t handle_fremovexattr(int fd, const char *name);

/* Event & Advanced I/O */
int64_t handle_eventfd2(unsigned int initval, int flags);
int64_t handle_preadv(int fd, const struct linux_iovec *iov, int iovcnt, off_t offset);
int64_t handle_pwritev(int fd, const struct linux_iovec *iov, int iovcnt, off_t offset);
int64_t handle_sendfile(int out_fd, int in_fd, off_t *offset, size_t count);
int64_t handle_splice(int fd_in, off_t *off_in, int fd_out, off_t *off_out, size_t len, unsigned int flags);
int64_t handle_tee(int fd_in, int fd_out, size_t len, unsigned int flags);
int64_t handle_sync_file_range(int fd, off_t offset, off_t nbytes, unsigned int flags);

/* Process, Clocks & Personality */
int64_t handle_personality(unsigned long persona);
int64_t handle_clock_getres(int clockid, struct linux_timespec *res);
int64_t handle_getrlimit(int resource, struct linux_rlimit *rlim);
int64_t handle_setrlimit(int resource, const struct linux_rlimit *rlim);
int64_t handle_prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5);
int64_t handle_getcpu(unsigned *cpu, unsigned *node, void *tcache);

/* Wave 4 Syscalls */
int64_t handle_mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev);
int64_t handle_vhangup(void);
int64_t handle_waitid(int which, id_t id, struct linux_siginfo *infop, int options, void *ru);
int64_t handle_sched_setparam(pid_t pid, const struct linux_sched_param *param);
int64_t handle_sched_getparam(pid_t pid, struct linux_sched_param *param);
int64_t handle_sched_setscheduler(pid_t pid, int policy, const struct linux_sched_param *param);
int64_t handle_sched_getscheduler(pid_t pid);
int64_t handle_sched_get_priority_max(int policy);
int64_t handle_sched_get_priority_min(int policy);
int64_t handle_sched_rr_get_interval(pid_t pid, struct linux_timespec *tp);
int64_t handle_rt_sigpending(void *set, size_t sigsetsize);
int64_t handle_setpriority(int which, int who, int prio);
int64_t handle_getpriority(int which, int who);
int64_t handle_setfsuid(uid_t fsuid);
int64_t handle_setfsgid(gid_t fsgid);
int64_t handle_sethostname(const char *name, size_t len);
int64_t handle_setdomainname(const char *name, size_t len);
int64_t handle_mremap(unsigned long old_address, unsigned long old_size, unsigned long new_size, unsigned long flags, unsigned long new_address);
int64_t handle_fadvise64(int fd, off_t offset, off_t len, int advice);
int64_t handle_msync(unsigned long start, size_t len, int flags);
int64_t handle_mlock(unsigned long addr, size_t len);
int64_t handle_munlock(unsigned long addr, size_t len);
int64_t handle_mlockall(int flags);
int64_t handle_munlockall(void);
int64_t handle_mincore(unsigned long start, size_t len, unsigned char *vec);

#endif /* SYSCALLS_H */

