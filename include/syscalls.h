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

/* Linux statx timestamp */
struct linux_statx_timestamp {
    int64_t  tv_sec;
    uint32_t tv_nsec;
    int32_t  __statx_pad1;
};

/* Linux statx (256 bytes) */
struct linux_statx {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __statx_pad1[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    struct linux_statx_timestamp stx_atime;
    struct linux_statx_timestamp stx_btime;
    struct linux_statx_timestamp stx_ctime;
    struct linux_statx_timestamp stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t stx_mnt_id;
    uint64_t __statx_pad2;
    uint64_t __statx_pad3[12];
};

/* Linux sembuf */
struct linux_sembuf {
    uint16_t sem_num;
    int16_t  sem_op;
    int16_t  sem_flg;
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

/* Wave 5 Syscalls */
int64_t handle_inotify_init1(int flags);
int64_t handle_inotify_add_watch(int fd, const char *pathname, uint32_t mask);
int64_t handle_inotify_rm_watch(int fd, int wd);
int64_t handle_ioprio_set(int which, int who, int ioprio);
int64_t handle_ioprio_get(int which, int who);
int64_t handle_timer_create(int clockid, void *sevp, int *timerid);
int64_t handle_timer_gettime(int timerid, struct linux_itimerspec *curr_value);
int64_t handle_timer_getoverrun(int timerid);
int64_t handle_timer_settime(int timerid, int flags, const struct linux_itimerspec *new_value, struct linux_itimerspec *old_value);
int64_t handle_timer_delete(int timerid);
int64_t handle_rt_sigsuspend(const uint64_t *unewset, size_t sigsetsize);
int64_t handle_rt_sigtimedwait(const uint64_t *uthese, struct linux_siginfo *uinfo, const struct linux_timespec *uts, size_t sigsetsize);
int64_t handle_rt_sigqueueinfo(pid_t tgid, int sig, struct linux_siginfo *uinfo);
int64_t handle_msgget(key_t key, int msgflg);
int64_t handle_msgctl(int msqid, int cmd, void *buf);
int64_t handle_msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg);
int64_t handle_msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg);
int64_t handle_semget(key_t key, int nsems, int semflg);
int64_t handle_semctl(int semid, int semnum, int cmd, unsigned long arg);
int64_t handle_semop(int semid, void *sops, size_t nsops);
int64_t handle_semtimedop(int semid, void *sops, size_t nsops, const struct linux_timespec *timeout);
int64_t handle_execveat(int dirfd, const char *pathname, char *const argv[], char *const envp[], int flags);
int64_t handle_preadv2(int fd, const struct linux_iovec *iov, int iovcnt, off_t offset, int flags);
int64_t handle_pwritev2(int fd, const struct linux_iovec *iov, int iovcnt, off_t offset, int flags);
int64_t handle_statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct linux_statx *statxbuf);
int64_t handle_pidfd_open(pid_t pid, unsigned int flags);

/* Wave 6 Structures */
struct linux_shmid_ds {
    struct {
        uint32_t key;
        uint32_t uid;
        uint32_t gid;
        uint32_t cuid;
        uint32_t cgid;
        uint32_t mode;
        uint16_t seq;
        uint16_t pad;
        uint64_t unused1;
        uint64_t unused2;
    } shm_perm;
    size_t   shm_segsz;
    int64_t  shm_atime;
    int64_t  shm_dtime;
    int64_t  shm_ctime;
    int32_t  shm_cpid;
    int32_t  shm_lpid;
    uint64_t shm_nattch;
    uint64_t unused4;
    uint64_t unused5;
};

struct linux_mq_attr {
    long mq_flags;
    long mq_maxmsg;
    long mq_msgsize;
    long mq_curmsgs;
    long __pad[4];
};

struct linux_mmsghdr {
    struct {
        void *msg_name;
        socklen_t msg_namelen;
        int __pad1;
        struct iovec *msg_iov;
        size_t msg_iovlen;
        void *msg_control;
        size_t msg_controllen;
        int msg_flags;
        int __pad2;
    } msg_hdr;
    unsigned int msg_len;
};

struct linux_timex {
    unsigned int modes;
    long offset;
    long freq;
    long maxerror;
    long esterror;
    int status;
    long constant;
    long precision;
    long tolerance;
    struct {
        long tv_sec;
        long tv_usec;
    } time;
    long tick;
    long ppsfreq;
    long jitter;
    int shift;
    long stabil;
    long jitcnt;
    long calcnt;
    long errcnt;
    long stbcnt;
    int tai;
    int pad[11];
};

/* Wave 6 Syscalls */
int64_t handle_shmget(key_t key, size_t size, int shmflg);
int64_t handle_shmctl(int shmid, int cmd, void *buf);
int64_t handle_shmat(int shmid, const void *shmaddr, int shmflg);
int64_t handle_shmdt(const void *shmaddr);
int64_t handle_mq_open(const char *name, int oflag, mode_t mode, struct linux_mq_attr *attr);
int64_t handle_mq_unlink(const char *name);
int64_t handle_mq_timedsend(int mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio, const struct linux_timespec *abs_timeout);
int64_t handle_mq_timedreceive(int mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio, const struct linux_timespec *abs_timeout);
int64_t handle_mq_notify(int mqdes, const void *notification);
int64_t handle_mq_getsetattr(int mqdes, const struct linux_mq_attr *newattr, struct linux_mq_attr *oldattr);
int64_t handle_sendmmsg(int sockfd, struct linux_mmsghdr *msgvec, unsigned int vlen, unsigned int flags);
int64_t handle_recvmmsg(int sockfd, struct linux_mmsghdr *msgvec, unsigned int vlen, unsigned int flags, struct linux_timespec *timeout);
int64_t handle_clock_settime(clockid_t clockid, const struct linux_timespec *tp);
int64_t handle_settimeofday(const struct timeval *tv, const struct timezone *tz);
int64_t handle_adjtimex(struct linux_timex *buf);
int64_t handle_clock_adjtime(clockid_t clk_id, struct linux_timex *buf);
int64_t handle_mlock2(unsigned long addr, size_t len, int flags);
int64_t handle_membarrier(int cmd, unsigned int flags, int cpu_id);
int64_t handle_userfaultfd(int flags);
int64_t handle_rseq(void *rseq, uint32_t rseq_len, int flags, uint32_t sig);
int64_t handle_sched_setattr(pid_t pid, void *attr, unsigned int flags);
int64_t handle_sched_getattr(pid_t pid, void *attr, unsigned int size, unsigned int flags);
int64_t handle_rt_tgsigqueueinfo(pid_t tgid, pid_t tid, int sig, struct linux_siginfo *uinfo);
int64_t handle_pidfd_send_signal(int pidfd, int sig, struct linux_siginfo *info, unsigned int flags);
int64_t handle_pidfd_getfd(int pidfd, int targetfd, unsigned int flags);

/* Wave 7: Linux AIO, System & Module Control, Robust Mutexes, Keys */
typedef unsigned long linux_aio_context_t;

struct linux_io_event {
    uint64_t data;
    uint64_t obj;
    int64_t  res;
    int64_t  res2;
};

struct linux_iocb {
    uint64_t aio_data;
    uint32_t aio_key;
    uint32_t aio_reserved1;
    uint16_t aio_lio_opcode;
    int16_t  aio_reqprio;
    int32_t  aio_fildes;
    uint64_t aio_buf;
    uint64_t aio_nbytes;
    int64_t  aio_offset;
    uint64_t aio_reserved2;
    uint32_t aio_flags;
    uint32_t aio_resfd;
};

struct linux_robust_list {
    struct linux_robust_list *next;
};

struct linux_robust_list_head {
    struct linux_robust_list list;
    long futex_offset;
    struct linux_robust_list *list_op_pending;
};

struct linux_kexec_segment {
    const void *buf;
    size_t bufsz;
    const void *mem;
    size_t memsz;
};

int64_t handle_io_setup(unsigned nr_events, linux_aio_context_t *ctx_idp);
int64_t handle_io_destroy(linux_aio_context_t ctx_id);
int64_t handle_io_submit(linux_aio_context_t ctx_id, long nr, struct linux_iocb **iocbpp);
int64_t handle_io_cancel(linux_aio_context_t ctx_id, struct linux_iocb *iocb, struct linux_io_event *result);
int64_t handle_io_getevents(linux_aio_context_t ctx_id, long min_nr, long nr, struct linux_io_event *events, struct linux_timespec *timeout);
int64_t handle_lookup_dcookie(uint64_t cookie, char *buf, size_t len);
int64_t handle_umount2(const char *target, int flags);
int64_t handle_mount(const char *dev_name, const char *dir_name, const char *type, unsigned long flags, const void *data);
int64_t handle_pivot_root(const char *new_root, const char *put_old);
int64_t handle_nfsservctl(int cmd, void *argp, void *resp);
int64_t handle_quotactl(int cmd, const char *special, int id, void *addr);
int64_t handle_vmsplice(int fd, const struct iovec *iov, unsigned long nr_segs, unsigned int flags);
int64_t handle_acct(const char *filename);
int64_t handle_unshare(int flags);
int64_t handle_set_robust_list(struct linux_robust_list_head *head, size_t len);
int64_t handle_get_robust_list(int pid, struct linux_robust_list_head **head_ptr, size_t *len_ptr);
int64_t handle_kexec_load(unsigned long entry, unsigned long nr_segments, struct linux_kexec_segment *segments, unsigned long flags);
int64_t handle_init_module(void *module_image, unsigned long len, const char *param_values);
int64_t handle_delete_module(const char *name_user, unsigned int flags);
int64_t handle_syslog(int type, char *bufp, int len);
int64_t handle_ptrace(long request, pid_t pid, void *addr, void *data);
int64_t handle_restart_syscall(void);
int64_t handle_reboot(int magic1, int magic2, int cmd, void *arg);
int64_t handle_add_key(const char *type, const char *description, const void *payload, size_t plen, int32_t ringid);
int64_t handle_request_key(const char *type, const char *description, const char *callout_info, int32_t destringid);

/* Wave 8: Final 25 Linux ARM64 Syscalls */
struct linux_file_handle {
    unsigned int handle_bytes;
    int handle_type;
    unsigned char f_handle[0];
};

struct linux_clone_args {
    uint64_t flags;
    uint64_t pidfd;
    uint64_t child_tid;
    uint64_t parent_tid;
    uint64_t exit_signal;
    uint64_t stack;
    uint64_t stack_size;
    uint64_t tls;
    uint64_t set_tid;
    uint64_t set_tid_size;
    uint64_t cgroup;
};

int64_t handle_keyctl(int cmd, unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5);
int64_t handle_swapon(const char *path, int swapflags);
int64_t handle_swapoff(const char *path);
int64_t handle_remap_file_pages(void *addr, size_t size, int prot, size_t pgoff, int flags);
int64_t handle_mbind(void *addr, unsigned long len, int mode, const unsigned long *nmask, unsigned long maxnode, unsigned flags);
int64_t handle_get_mempolicy(int *mode, unsigned long *nmask, unsigned long maxnode, void *addr, unsigned long flags);
int64_t handle_set_mempolicy(int mode, const unsigned long *nmask, unsigned long maxnode);
int64_t handle_migrate_pages(pid_t pid, unsigned long maxnode, const unsigned long *from, const unsigned long *to);
int64_t handle_move_pages(pid_t pid, unsigned long nr_pages, const void **pages, const int *nodes, int *status, int flags);
int64_t handle_perf_event_open(void *attr_uptr, pid_t pid, int cpu, int group_fd, unsigned long flags);
int64_t handle_fanotify_init(unsigned int flags, unsigned int event_f_flags);
int64_t handle_fanotify_mark(int fanotify_fd, unsigned int flags, uint64_t mask, int dirfd, const char *pathname);
int64_t handle_name_to_handle_at(int dirfd, const char *pathname, struct linux_file_handle *handle, int *mount_id, int flags);
int64_t handle_open_by_handle_at(int mountdirfd, struct linux_file_handle *handle, int flags);
int64_t handle_setns(int fd, int nstype);
int64_t handle_process_vm_readv(pid_t pid, const struct iovec *local_iov, unsigned long liovcnt, const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags);
int64_t handle_process_vm_writev(pid_t pid, const struct iovec *local_iov, unsigned long liovcnt, const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags);
int64_t handle_kcmp(pid_t pid1, pid_t pid2, int type, unsigned long idx1, unsigned long idx2);
int64_t handle_finit_module(int fd, const char *param_values, int flags);
int64_t handle_seccomp(unsigned int operation, unsigned int flags, void *args);
int64_t handle_bpf(int cmd, void *uattr, unsigned int size);
int64_t handle_pkey_mprotect(void *addr, size_t len, int prot, int pkey);
int64_t handle_pkey_alloc(unsigned int flags, unsigned int access_rights);
int64_t handle_pkey_free(int pkey);
int64_t handle_clone3(struct linux_clone_args *cl_args, size_t size, void *state);

/* Wave 9 (Linux 6.12 Modern Syscalls 425-433, 440-462) Structures & Prototypes */
struct linux_cachestat_range {
    uint64_t off;
    uint64_t len;
};

struct linux_cachestat {
    uint64_t nr_cache;
    uint64_t nr_dirty;
    uint64_t nr_writeback;
    uint64_t nr_evicted;
    uint64_t nr_recently_evicted;
};

struct linux_futex_waitv {
    uint64_t val;
    uint64_t uaddr;
    uint32_t flags;
    uint32_t __reserved;
};

int64_t handle_io_uring_setup(uint32_t entries, void *params);
int64_t handle_io_uring_enter(unsigned int fd, unsigned int to_submit, unsigned int min_complete, unsigned int flags, void *sig);
int64_t handle_io_uring_register(unsigned int fd, unsigned int opcode, void *arg, unsigned int nr_args);
int64_t handle_open_tree(int dfd, const char *filename, unsigned int flags);
int64_t handle_move_mount(int from_dfd, const char *from_path, int to_dfd, const char *to_path, unsigned int flags);
int64_t handle_fsopen(const char *fs_name, unsigned int flags);
int64_t handle_fsconfig(int fd, unsigned int cmd, const char *key, const void *value, int aux);
int64_t handle_fsmount(int fs_fd, unsigned int flags, unsigned int attr_flags);
int64_t handle_fspick(int dfd, const char *path, unsigned int flags);

int64_t handle_process_madvise(int pidfd, const struct iovec *iovec, size_t vlen, int advice, unsigned int flags);
int64_t handle_epoll_pwait2(int epfd, void *events, int maxevents, const void *timeout, const void *sigmask);
int64_t handle_mount_setattr(int dfd, const char *path, unsigned int flags, void *uattr, size_t usize);
int64_t handle_quotactl_fd(int fd, unsigned int cmd, int id, void *addr);
int64_t handle_landlock_create_ruleset(const void *attr, size_t size, uint32_t flags);
int64_t handle_landlock_add_rule(int ruleset_fd, int rule_type, const void *rule_attr, uint32_t flags);
int64_t handle_landlock_restrict_self(int ruleset_fd, uint32_t flags);
int64_t handle_memfd_secret(unsigned int flags);
int64_t handle_process_mrelease(int pidfd, unsigned int flags);
int64_t handle_futex_waitv(struct linux_futex_waitv *waiters, unsigned int nr_futexes, unsigned int flags, const void *timeout, clockid_t clockid);
int64_t handle_set_mempolicy_home_node(unsigned long start, unsigned long len, unsigned long home_node, unsigned long flags);
int64_t handle_cachestat(unsigned int fd, struct linux_cachestat_range *cstat_range, struct linux_cachestat *cstat, unsigned int flags);
int64_t handle_fchmodat2(int dirfd, const char *pathname, mode_t mode, int flags);
int64_t handle_map_shadow_stack(unsigned long addr, unsigned long size, unsigned int flags);
int64_t handle_futex_wake(volatile uint32_t *uaddr, unsigned long mask, int nr_wake);
int64_t handle_futex_wait(volatile uint32_t *uaddr, unsigned long mask, uint32_t val, const void *timeout);
int64_t handle_futex_requeue(volatile uint32_t *uaddr, unsigned long mask, int nr_wake, volatile uint32_t *uaddr2, int nr_requeue);
int64_t handle_statmount(const void *req, void *buf, size_t bufsize, unsigned int flags);
int64_t handle_listmount(const void *req, uint64_t *mnt_ids, size_t nr_mnt_ids, unsigned int flags);
int64_t handle_lsm_get_self_attr(unsigned int attr, void *ctx, size_t *size, uint32_t flags);
int64_t handle_lsm_set_self_attr(unsigned int attr, const void *ctx, size_t size, uint32_t flags);
int64_t handle_lsm_list_modules(uint64_t *ids, size_t *size, uint32_t flags);
int64_t handle_mseal(unsigned long start, size_t len, unsigned long flags);

#endif /* SYSCALLS_H */

