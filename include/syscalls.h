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

#endif /* SYSCALLS_H */
