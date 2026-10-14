#include "syscalls.h"
#include "sandbox.h"
#include "patcher.h"
#include "elf_loader.h"
#include <termios.h>
#include <mach-o/dyld.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

/* Futex bucket structure */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool initialized;
} futex_bucket_t;

#define FUTEX_BUCKETS 64
static futex_bucket_t g_futex_table[FUTEX_BUCKETS];

static void init_futex_bucket(futex_bucket_t *b) {
    if (!b->initialized) {
        pthread_mutex_init(&b->mutex, NULL);
        pthread_cond_init(&b->cond, NULL);
        b->initialized = true;
    }
}

/* Directory descriptor cache for getdents64 */
#define DIR_CACHE_MAX 256
static DIR *g_dir_cache[DIR_CACHE_MAX];

/* File I/O */
int64_t handle_read(int fd, void *buf, size_t count) {
    ssize_t ret = read(fd, buf, count);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_write(int fd, const void *buf, size_t count) {
    ssize_t ret = write(fd, buf, count);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (!pathname) return -LINUX_EINVAL;

    /* Translate Linux open flags to Darwin open flags */
    int dflags = 0;
    int acc = flags & 3;
    if (acc == LINUX_O_RDONLY) dflags |= O_RDONLY;
    else if (acc == LINUX_O_WRONLY) dflags |= O_WRONLY;
    else if (acc == LINUX_O_RDWR) dflags |= O_RDWR;

    if (flags & LINUX_O_CREAT)     dflags |= O_CREAT;
    if (flags & LINUX_O_EXCL)      dflags |= O_EXCL;
    if (flags & LINUX_O_NOCTTY)    dflags |= O_NOCTTY;
    if (flags & LINUX_O_TRUNC)     dflags |= O_TRUNC;
    if (flags & LINUX_O_APPEND)    dflags |= O_APPEND;
    if (flags & LINUX_O_NONBLOCK)  dflags |= O_NONBLOCK;
    if (flags & LINUX_O_DIRECTORY) dflags |= O_DIRECTORY;
    if (flags & LINUX_O_CLOEXEC)   dflags |= O_CLOEXEC;

    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), flags);
    if (res < 0) return res;

    if (res == 1) {
        /* Virtual path */
        return sandbox_open_virtual(pathname, dflags, mode);
    }

    int fd = open(host_path, dflags, mode);
    if (fd < 0) return darwin_to_linux_errno(errno);
    return fd;
}

int64_t handle_close(int fd) {
    if (fd >= 0 && fd < DIR_CACHE_MAX && g_dir_cache[fd]) {
        closedir(g_dir_cache[fd]);
        g_dir_cache[fd] = NULL;
    }
    int ret = close(fd);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

static void copy_stat_to_linux(const struct stat *st, struct linux_stat *statbuf) {
    memset(statbuf, 0, sizeof(*statbuf));
    statbuf->st_dev = st->st_dev;
    statbuf->st_ino = st->st_ino;
    statbuf->st_mode = st->st_mode;
    statbuf->st_nlink = st->st_nlink;
    statbuf->st_uid = 1000;
    statbuf->st_gid = 1000;
    statbuf->st_rdev = st->st_rdev;
    statbuf->st_size = st->st_size;
    statbuf->st_blksize = st->st_blksize > 0 ? st->st_blksize : 4096;
    statbuf->st_blocks = st->st_blocks;
    statbuf->st_atime_sec = st->st_atimespec.tv_sec;
    statbuf->st_atime_nsec = st->st_atimespec.tv_nsec;
    statbuf->st_mtime_sec = st->st_mtimespec.tv_sec;
    statbuf->st_mtime_nsec = st->st_mtimespec.tv_nsec;
    statbuf->st_ctime_sec = st->st_ctimespec.tv_sec;
    statbuf->st_ctime_nsec = st->st_ctimespec.tv_nsec;
}

int64_t handle_fstat(int fd, struct linux_stat *statbuf) {
    if (!statbuf) return -LINUX_EFAULT;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        return darwin_to_linux_errno(errno);
    }
    copy_stat_to_linux(&st, statbuf);
    return 0;
}

int64_t handle_newfstatat(int dirfd, const char *pathname, struct linux_stat *statbuf, int flags) {
    if (!statbuf) return -LINUX_EFAULT;

    if (pathname && sandbox_is_virtual_path(pathname)) {
        struct stat st;
        int vres = sandbox_virtual_stat(pathname, &st);
        if (vres < 0) return vres;
        copy_stat_to_linux(&st, statbuf);
        return 0;
    }

    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname ? pathname : "", host_path, sizeof(host_path), flags);
    if (res < 0) return res;

    int dflags = 0;
    if (flags & 0x100) dflags |= AT_SYMLINK_NOFOLLOW; /* Linux AT_SYMLINK_NOFOLLOW is 0x100 */

    struct stat st;
    if (fstatat(AT_FDCWD, host_path, &st, dflags) != 0) {
        return darwin_to_linux_errno(errno);
    }
    copy_stat_to_linux(&st, statbuf);
    return 0;
}

int64_t handle_lseek(int fd, off_t offset, int whence) {
    off_t res = lseek(fd, offset, whence);
    if (res < 0) return darwin_to_linux_errno(errno);
    return res;
}

int64_t handle_ioctl(int fd, unsigned long request, void *arg) {
    /* TIOCGWINSZ: Linux 0x5413 */
    if (request == 0x5413) {
        struct winsize ws;
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0) {
            if (arg) {
                struct linux_winsize *lws = (struct linux_winsize *)arg;
                lws->ws_row = ws.ws_row;
                lws->ws_col = ws.ws_col;
                lws->ws_xpixel = ws.ws_xpixel;
                lws->ws_ypixel = ws.ws_ypixel;
            }
            return 0;
        }
        return darwin_to_linux_errno(errno);
    }

    /* TCGETS: Linux 0x5401 */
    if (request == 0x5401) {
        struct termios tios;
        if (tcgetattr(fd, &tios) == 0) {
            return 0;
        }
        return darwin_to_linux_errno(errno);
    }

    /* TIOCGPGRP: Linux 0x540f */
    if (request == 0x540f) {
        pid_t pgrp = tcgetpgrp(fd);
        if (pgrp < 0) return darwin_to_linux_errno(errno);
        if (arg) *(pid_t *)arg = pgrp;
        return 0;
    }

    /* TIOCSPGRP: Linux 0x5410 */
    if (request == 0x5410) {
        if (!arg) return -LINUX_EINVAL;
        if (tcsetpgrp(fd, *(pid_t *)arg) != 0) return darwin_to_linux_errno(errno);
        return 0;
    }

    return -LINUX_ENOTTY;
}

int64_t handle_readv(int fd, const struct linux_iovec *iov, int iovcnt) {
    if (!iov || iovcnt < 0 || iovcnt > 1024) return -LINUX_EINVAL;

    struct iovec diov[iovcnt];
    for (int i = 0; i < iovcnt; i++) {
        diov[i].iov_base = (void *)iov[i].iov_base;
        diov[i].iov_len = (size_t)iov[i].iov_len;
    }

    ssize_t ret = readv(fd, diov, iovcnt);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_writev(int fd, const struct linux_iovec *iov, int iovcnt) {
    if (!iov || iovcnt < 0 || iovcnt > 1024) return -LINUX_EINVAL;

    struct iovec diov[iovcnt];
    for (int i = 0; i < iovcnt; i++) {
        diov[i].iov_base = (void *)iov[i].iov_base;
        diov[i].iov_len = (size_t)iov[i].iov_len;
    }

    ssize_t ret = writev(fd, diov, iovcnt);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_dup(int oldfd) {
    int ret = dup(oldfd);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_dup3(int oldfd, int newfd, int flags) {
    if (oldfd == newfd) return -LINUX_EINVAL;
    int ret = dup2(oldfd, newfd);
    if (ret < 0) return darwin_to_linux_errno(errno);

    if (flags & LINUX_O_CLOEXEC) {
        fcntl(newfd, F_SETFD, FD_CLOEXEC);
    }
    return ret;
}

int64_t handle_fcntl(int fd, int cmd, unsigned long arg) {
    int dcmd = cmd;
    if (cmd == 1030) { /* F_DUPFD_CLOEXEC */
        dcmd = F_DUPFD_CLOEXEC;
    }

    int ret = fcntl(fd, dcmd, arg);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_getdents64(int fd, void *dirp, size_t count) {
    if (!dirp || count == 0) return -LINUX_EINVAL;

    DIR *d = NULL;
    if (fd >= 0 && fd < DIR_CACHE_MAX && g_dir_cache[fd]) {
        d = g_dir_cache[fd];
    } else {
        int dupfd = dup(fd);
        if (dupfd < 0) return darwin_to_linux_errno(errno);
        d = fdopendir(dupfd);
        if (!d) {
            close(dupfd);
            return darwin_to_linux_errno(errno);
        }
        if (fd >= 0 && fd < DIR_CACHE_MAX) {
            g_dir_cache[fd] = d;
        }
    }

    size_t bytes_written = 0;
    char *out = (char *)dirp;

    while (bytes_written < count) {
        long loc_before = telldir(d);
        struct dirent *de = readdir(d);
        if (!de) break;

        size_t namelen = strlen(de->d_name);
        /* 19 bytes header + namelen + 1 null byte, aligned to 8 bytes */
        size_t reclen = (19 + namelen + 1 + 7) & ~7ULL;
        if (bytes_written + reclen > count) {
            /* Rewind to before this entry if it doesn't fit */
            seekdir(d, loc_before);
            break;
        }

        struct linux_dirent64 *ld = (struct linux_dirent64 *)(out + bytes_written);
        ld->d_ino = de->d_ino;
        ld->d_off = telldir(d);
        ld->d_reclen = (uint16_t)reclen;
        ld->d_type = de->d_type;
        memcpy(ld->d_name, de->d_name, namelen + 1);

        bytes_written += reclen;
    }

    return bytes_written;
}

int64_t handle_ppoll(struct pollfd *fds, nfds_t nfds, const struct linux_timespec *tmo_p, const void *sigmask) {
    (void)sigmask;
    int timeout_ms = -1;
    if (tmo_p) {
        timeout_ms = (int)(tmo_p->tv_sec * 1000 + tmo_p->tv_nsec / 1000000);
    }
    int ret = poll(fds, nfds, timeout_ms);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_tkill(int tid, int sig) {
    (void)tid;
    if (sig == 0) return 0;
    int ret = kill(getpid(), sig);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_tgkill(int tgid, int tid, int sig) {
    (void)tgid;
    return handle_tkill(tid, sig);
}

int64_t handle_sigaltstack(const void *ss, void *oss) {
    (void)ss;
    if (oss) {
        /* Linux stack_t: void *ss_sp (0), int ss_flags (8), size_t ss_size (16) */
        struct {
            void *ss_sp;
            int ss_flags;
            int __pad;
            size_t ss_size;
        } *out = (void *)oss;
        out->ss_sp = NULL;
        out->ss_flags = 2; /* SS_DISABLE */
        out->ss_size = 0;
    }
    return 0;
}

/* Memory Management */
int64_t handle_brk(uintptr_t brk_addr) {
    if (brk_addr == 0 || brk_addr < g_ltm.brk_start) {
        return (int64_t)g_ltm.brk_current;
    }

    if (brk_addr > g_ltm.brk_current) {
        uintptr_t page_cur = PAGE_ROUND_UP(g_ltm.brk_current);
        uintptr_t page_new = PAGE_ROUND_UP(brk_addr);

        if (page_new > page_cur) {
            void *p = mmap((void *)page_cur, page_new - page_cur,
                           PROT_READ | PROT_WRITE,
                           MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
            if (p == MAP_FAILED) {
                ltm_log_error("brk mmap failed for expansion to %p", (void *)page_new);
                return (int64_t)g_ltm.brk_current;
            }
        }
        g_ltm.brk_current = brk_addr;
        return (int64_t)g_ltm.brk_current;
    }

    if (brk_addr < g_ltm.brk_current) {
        uintptr_t page_cur = PAGE_ROUND_UP(g_ltm.brk_current);
        uintptr_t page_new = PAGE_ROUND_UP(brk_addr);

        if (page_cur > page_new) {
            munmap((void *)page_new, page_cur - page_new);
        }
        g_ltm.brk_current = brk_addr;
        return (int64_t)g_ltm.brk_current;
    }

    return (int64_t)g_ltm.brk_current;
}

int64_t handle_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    if (length == 0) return -LINUX_EINVAL;

    size_t aligned_len = PAGE_ROUND_UP(length);
    int dflags = 0;

    if (flags & LINUX_MAP_SHARED)     dflags |= MAP_SHARED;
    if (flags & LINUX_MAP_PRIVATE)    dflags |= MAP_PRIVATE;
    if (flags & LINUX_MAP_FIXED)      dflags |= MAP_FIXED;
    if (flags & LINUX_MAP_ANONYMOUS) {
        dflags |= MAP_ANON;
        fd = -1;
        offset = 0;
    }

    /* On Apple Silicon ARM64, anonymous memory with execution requires MAP_JIT (unless MAP_FIXED) */
    if ((dflags & MAP_ANON) && !(dflags & MAP_FIXED)) {
        dflags |= MAP_JIT;
    }

    /* Ignore non-fixed hints below 4GB */
    if (!(flags & LINUX_MAP_FIXED) && (uintptr_t)addr < 0x100000000ULL) {
        addr = NULL;
    } else if ((flags & LINUX_MAP_FIXED) && (uintptr_t)addr < 0x100000000ULL) {
        return -LINUX_ENOMEM;
    }

    void *res = mmap(addr, aligned_len, prot, dflags, fd, offset);
    if (res == MAP_FAILED) {
        return darwin_to_linux_errno(errno);
    }

    if (prot & PROT_EXEC) {
        size_t patch_len = aligned_len;
        if (fd >= 0) {
            struct stat st;
            if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
                if ((off_t)offset < st.st_size) {
                    size_t avail = (size_t)(st.st_size - offset);
                    if (patch_len > avail) {
                        patch_len = avail;
                    }
                } else {
                    patch_len = 0;
                }
            }
        }
        if (patch_len > 0) {
            patcher_patch_executable_range(res, patch_len);
        }
    }

    return (int64_t)(uintptr_t)res;
}

int64_t handle_munmap(void *addr, size_t length) {
    if (!addr || length == 0) return -LINUX_EINVAL;
    size_t aligned_len = PAGE_ROUND_UP(length);

    if (munmap(addr, aligned_len) != 0) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_mprotect(void *addr, size_t length, int prot) {
    if (!addr || length == 0) return -LINUX_EINVAL;

    uintptr_t pstart = PAGE_ROUND_DOWN(addr);
    uintptr_t pend = PAGE_ROUND_UP((uintptr_t)addr + length);
    size_t plen = pend - pstart;

    if (mprotect((void *)pstart, plen, prot) != 0) {
        return darwin_to_linux_errno(errno);
    }

    if (prot & PROT_EXEC) {
        patcher_patch_executable_range((void *)pstart, plen);
    }

    return 0;
}

/* Identity */
int64_t handle_getpid(void) {
    return getpid();
}

int64_t handle_getppid(void) {
    return getppid();
}

int64_t handle_getuid(void) {
    return 1000;
}

int64_t handle_geteuid(void) {
    return 1000;
}

int64_t handle_getgid(void) {
    return 1000;
}

int64_t handle_getegid(void) {
    return 1000;
}

int64_t handle_gettid(void) {
    return getpid();
}

int64_t handle_uname(struct linux_utsname *buf) {
    if (!buf) return -LINUX_EFAULT;
    memset(buf, 0, sizeof(*buf));

    strncpy(buf->sysname, "Linux", sizeof(buf->sysname) - 1);
    strncpy(buf->nodename, "ltm-mac", sizeof(buf->nodename) - 1);
    strncpy(buf->release, "6.6.0-ltm", sizeof(buf->release) - 1);
    strncpy(buf->version, "#1 SMP PREEMPT_DYNAMIC Wed Oct 14 00:00:00 UTC 2026", sizeof(buf->version) - 1);
    strncpy(buf->machine, "aarch64", sizeof(buf->machine) - 1);
    strncpy(buf->domainname, "(none)", sizeof(buf->domainname) - 1);

    return 0;
}

int64_t handle_sysinfo(struct linux_sysinfo *info) {
    if (!info) return -LINUX_EFAULT;
    memset(info, 0, sizeof(*info));

    info->uptime = 3600;
    uint64_t memsize = 16ULL * 1024 * 1024 * 1024;
    size_t size = sizeof(memsize);
    sysctlbyname("hw.memsize", &memsize, &size, NULL, 0);

    info->totalram = memsize;
    info->freeram = memsize / 2;
    info->procs = 1;
    info->mem_unit = 1;

    return 0;
}

/* Time */
int64_t handle_clock_gettime(int clock_id, struct linux_timespec *tp) {
    if (!tp) return -LINUX_EFAULT;

    clockid_t dclk = CLOCK_REALTIME;
    if (clock_id == 1 || clock_id == 4) { /* CLOCK_MONOTONIC / CLOCK_MONOTONIC_RAW */
        dclk = CLOCK_MONOTONIC;
    }

    struct timespec ts;
    if (clock_gettime(dclk, &ts) != 0) {
        return darwin_to_linux_errno(errno);
    }

    tp->tv_sec = ts.tv_sec;
    tp->tv_nsec = ts.tv_nsec;
    return 0;
}

int64_t handle_gettimeofday(struct linux_timeval *tv, struct linux_timezone *tz) {
    struct timeval dtv;
    struct timezone dtz;

    if (gettimeofday(&dtv, tz ? &dtz : NULL) != 0) {
        return darwin_to_linux_errno(errno);
    }

    if (tv) {
        tv->tv_sec = dtv.tv_sec;
        tv->tv_usec = dtv.tv_usec;
    }
    if (tz) {
        tz->tz_minuteswest = dtz.tz_minuteswest;
        tz->tz_dsttime = dtz.tz_dsttime;
    }
    return 0;
}

int64_t handle_nanosleep(const struct linux_timespec *req, struct linux_timespec *rem) {
    if (!req) return -LINUX_EFAULT;

    struct timespec dreq = { req->tv_sec, req->tv_nsec };
    struct timespec drem;

    int ret = nanosleep(&dreq, rem ? &drem : NULL);
    if (ret != 0) {
        if (rem) {
            rem->tv_sec = drem.tv_sec;
            rem->tv_nsec = drem.tv_nsec;
        }
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

/* Exit */
int64_t handle_exit(int status) {
    ltm_trace("sys_exit(%d)", status);
    if (g_ltm.child_tid_ptr) {
        *g_ltm.child_tid_ptr = 0;
        handle_futex((uint32_t *)g_ltm.child_tid_ptr, LINUX_FUTEX_WAKE, 1, NULL, NULL, 0);
    }
    exit(status);
}

int64_t handle_exit_group(int status) {
    ltm_trace("sys_exit_group(%d)", status);
    return handle_exit(status);
}

/* Threading & Futex */
int64_t handle_set_tid_address(int *tidptr) {
    g_ltm.child_tid_ptr = tidptr;
    return getpid();
}

int64_t handle_futex(uint32_t *uaddr, int op, uint32_t val,
                     const struct linux_timespec *timeout, uint32_t *uaddr2, uint32_t val3) {
    (void)uaddr2;
    (void)val3;
    if (!uaddr) return -LINUX_EFAULT;

    int cmd = op & ~(LINUX_FUTEX_PRIVATE_FLAG | LINUX_FUTEX_CLOCK_REALTIME);
    uint32_t bucket_idx = ((uintptr_t)uaddr >> 2) % FUTEX_BUCKETS;
    futex_bucket_t *b = &g_futex_table[bucket_idx];
    init_futex_bucket(b);

    if (cmd == LINUX_FUTEX_WAIT) {
        pthread_mutex_lock(&b->mutex);
        if (*(volatile uint32_t *)uaddr != val) {
            pthread_mutex_unlock(&b->mutex);
            return -LINUX_EAGAIN;
        }

        int r = 0;
        if (timeout) {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            struct timespec abstime;
            abstime.tv_sec = now.tv_sec + timeout->tv_sec;
            abstime.tv_nsec = now.tv_nsec + timeout->tv_nsec;
            if (abstime.tv_nsec >= 1000000000L) {
                abstime.tv_sec += 1;
                abstime.tv_nsec -= 1000000000L;
            }
            r = pthread_cond_timedwait(&b->cond, &b->mutex, &abstime);
        } else {
            r = pthread_cond_wait(&b->cond, &b->mutex);
        }
        pthread_mutex_unlock(&b->mutex);

        if (r == ETIMEDOUT) return -LINUX_ETIMEDOUT;
        return 0;
    }

    if (cmd == LINUX_FUTEX_WAKE) {
        pthread_mutex_lock(&b->mutex);
        pthread_cond_broadcast(&b->cond);
        pthread_mutex_unlock(&b->mutex);
        return val > 0 ? (int64_t)val : 1;
    }

    return -LINUX_ENOSYS;
}

int64_t handle_clone(uint64_t flags, void *stack, int *ptid, void *tls, int *ctid, void *state_ptr) {
    arm_thread_state64_t *state = (arm_thread_state64_t *)state_ptr;

    pid_t pid = fork();
    if (pid < 0) return darwin_to_linux_errno(errno);

    if (pid == 0) {
        /* Child process: re-init signal dispatcher and resume execution directly */
        syscall_dispatcher_init();

        if (state) {
            state->__x[0] = 0;
            state->__pc += 4;
            if (stack) {
                state->__sp = (uintptr_t)stack;
            }
        }
        if ((flags & LINUX_CLONE_SETTLS) && tls) {
            g_ltm.guest_tls = (uintptr_t)tls;
        }
        if ((flags & LINUX_CLONE_CHILD_SETTID) && ctid) {
            *ctid = getpid();
        }
        if (flags & LINUX_CLONE_CHILD_CLEARTID) {
            g_ltm.child_tid_ptr = ctid;
        }

        if (state) {
            ltm_resume_context(state);
        }
        exit(0);
    }

    /* Parent process */
    if ((flags & LINUX_CLONE_PARENT_SETTID) && ptid) {
        *ptid = pid;
    }

    return pid;
}

int64_t handle_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz) {
    if (!pathname || !buf || bufsiz == 0) return -LINUX_EINVAL;

    if (sandbox_is_virtual_path(pathname)) {
        return sandbox_virtual_readlink(pathname, buf, bufsiz);
    }

    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), AT_SYMLINK_NOFOLLOW);
    if (res < 0) return res;

    ssize_t ret = readlink(host_path, buf, bufsiz);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_prlimit64(int pid, int resource, const struct linux_rlimit64 *new_limit, struct linux_rlimit64 *old_limit) {
    (void)pid;
    (void)new_limit;
    if (old_limit) {
        if (resource == 3) { /* RLIMIT_STACK */
            old_limit->rlim_cur = 8 * 1024 * 1024;
            old_limit->rlim_max = 8 * 1024 * 1024;
        } else {
            old_limit->rlim_cur = 1024;
            old_limit->rlim_max = 1024;
        }
    }
    return 0;
}

int64_t handle_getrandom(void *buf, size_t buflen, unsigned int flags) {
    (void)flags;
    if (!buf) return -LINUX_EFAULT;
    arc4random_buf(buf, buflen);
    return buflen;
}

int64_t handle_wait4(pid_t pid, int *status, int options, void *rusage) {
    int dstatus = 0;
    pid_t ret = wait4(pid, &dstatus, options, (struct rusage *)rusage);
    if (ret < 0) {
        return darwin_to_linux_errno(errno);
    }
    if (status) {
        *status = dstatus;
    }
    return ret;
}

int64_t handle_pipe2(int pipefd[2], int flags) {
    if (!pipefd) return -LINUX_EFAULT;
    int fds[2];
    if (pipe(fds) != 0) {
        return darwin_to_linux_errno(errno);
    }
    if (flags & LINUX_O_CLOEXEC) {
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }
    if (flags & LINUX_O_NONBLOCK) {
        int fl0 = fcntl(fds[0], F_GETFL, 0);
        int fl1 = fcntl(fds[1], F_GETFL, 0);
        fcntl(fds[0], F_SETFL, fl0 | O_NONBLOCK);
        fcntl(fds[1], F_SETFL, fl1 | O_NONBLOCK);
    }
    pipefd[0] = fds[0];
    pipefd[1] = fds[1];
    return 0;
}

int64_t handle_getcwd(char *buf, size_t size) {
    if (!buf || size == 0) return -LINUX_EINVAL;
    char host_cwd[1024];
    if (!getcwd(host_cwd, sizeof(host_cwd))) {
        return darwin_to_linux_errno(errno);
    }
    size_t root_len = strlen(g_ltm.host_rootfs);
    const char *guest_cwd = "/";
    if (strncmp(host_cwd, g_ltm.host_rootfs, root_len) == 0) {
        guest_cwd = host_cwd + root_len;
        if (guest_cwd[0] == '\0') {
            guest_cwd = "/";
        }
    }
    size_t len = strlen(guest_cwd) + 1;
    if (len > size) return -LINUX_ERANGE;
    memcpy(buf, guest_cwd, len);
    return len;
}

int64_t handle_chdir(const char *path) {
    if (!path) return -LINUX_EINVAL;
    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (chdir(host_path) != 0) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_fchdir(int fd) {
    if (fchdir(fd) != 0) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_setpgid(pid_t pid, pid_t pgid) {
    if (setpgid(pid, pgid) != 0) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_getpgid(pid_t pid) {
    pid_t ret = getpgid(pid);
    if (ret < 0) {
        return darwin_to_linux_errno(errno);
    }
    return ret;
}

int64_t handle_execve(const char *pathname, char *const argv[], char *const envp[]) {
    if (!pathname) return -LINUX_EINVAL;

    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, pathname, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) return -LINUX_EACCES;

    if (access(host_path, X_OK) != 0) {
        return darwin_to_linux_errno(errno);
    }

    char ltm_path[1024];
    uint32_t path_size = sizeof(ltm_path);
    if (_NSGetExecutablePath(ltm_path, &path_size) != 0) {
        return -LINUX_ENOENT;
    }

    int guest_argc = 0;
    while (argv && argv[guest_argc]) {
        guest_argc++;
    }

    int max_args = guest_argc + 8;
    char **new_argv = malloc(max_args * sizeof(char *));
    if (!new_argv) return -LINUX_ENOMEM;

    int n = 0;
    new_argv[n++] = ltm_path;
    new_argv[n++] = "-r";
    new_argv[n++] = g_ltm.host_rootfs;
    if (g_ltm.trace_syscalls) {
        new_argv[n++] = "-s";
    }
    new_argv[n++] = (char *)pathname;

    for (int i = 0; i < guest_argc; i++) {
        ltm_log_info("execve guest argv[%d] = '%s'", i, argv[i] ? argv[i] : "(null)");
    }

    for (int i = 1; i < guest_argc; i++) {
        new_argv[n++] = argv[i];
    }
    new_argv[n] = NULL;

    for (int i = 0; i < n; i++) {
        ltm_log_info("execve target new_argv[%d] = '%s'", i, new_argv[i] ? new_argv[i] : "(null)");
    }

    ltm_trace("execve re-executing ltm: %s -r %s %s ...", ltm_path, g_ltm.host_rootfs, host_path);
    execve(ltm_path, new_argv, envp);

    int err = errno;
    free(new_argv);
    return darwin_to_linux_errno(err);
}

int64_t handle_unlinkat(int dirfd, const char *pathname, int flags) {
    if (!pathname) return -LINUX_EINVAL;
    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), AT_SYMLINK_NOFOLLOW);
    if (res < 0) return res;
    if (res == 1) return -LINUX_EPERM;

    int dflags = 0;
    if (flags & LINUX_AT_REMOVEDIR) {
        dflags |= AT_REMOVEDIR;
    }
    int ret = unlinkat(AT_FDCWD, host_path, dflags);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_mkdirat(int dirfd, const char *pathname, mode_t mode) {
    if (!pathname) return -LINUX_EINVAL;
    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) return -LINUX_EEXIST;

    int ret = mkdir(host_path, mode);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_faccessat(int dirfd, const char *pathname, int mode, int flags) {
    (void)flags;
    if (!pathname) return -LINUX_EINVAL;
    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) return 0;

    int ret = access(host_path, mode & (R_OK | W_OK | X_OK));
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    if (!oldpath || !newpath) return -LINUX_EINVAL;
    char old_host[1024], new_host[1024];
    int r1 = sandbox_resolve_path(olddirfd, oldpath, old_host, sizeof(old_host), 0);
    if (r1 < 0) return r1;
    if (r1 == 1) return -LINUX_EPERM;
    int r2 = sandbox_resolve_path(newdirfd, newpath, new_host, sizeof(new_host), 0);
    if (r2 < 0) return r2;
    if (r2 == 1) return -LINUX_EPERM;

    int ret = rename(old_host, new_host);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_renameat2(int olddirfd, const char *oldpath, int newdirfd, const char *newpath, unsigned int flags) {
    (void)flags;
    return handle_renameat(olddirfd, oldpath, newdirfd, newpath);
}

int64_t handle_symlinkat(const char *target, int newdirfd, const char *linkpath) {
    if (!target || !linkpath) return -LINUX_EINVAL;
    char link_host[1024];
    int res = sandbox_resolve_path(newdirfd, linkpath, link_host, sizeof(link_host), 0);
    if (res < 0) return res;
    if (res == 1) return -LINUX_EPERM;

    int ret = symlink(target, link_host);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_linkat(int olddirfd, const char *oldpath, int newdirfd, const char *linkpath, int flags) {
    (void)flags;
    if (!oldpath || !linkpath) return -LINUX_EINVAL;
    char old_host[1024], link_host[1024];
    int r1 = sandbox_resolve_path(olddirfd, oldpath, old_host, sizeof(old_host), 0);
    if (r1 < 0) return r1;
    if (r1 == 1) return -LINUX_EPERM;
    int r2 = sandbox_resolve_path(newdirfd, linkpath, link_host, sizeof(link_host), 0);
    if (r2 < 0) return r2;
    if (r2 == 1) return -LINUX_EPERM;

    int ret = link(old_host, link_host);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags) {
    (void)flags;
    if (!pathname) return -LINUX_EINVAL;
    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) return 0;

    int ret = chmod(host_path, mode);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_fchmod(int fd, mode_t mode) {
    int ret = fchmod(fd, mode);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags) {
    (void)dirfd; (void)pathname; (void)owner; (void)group; (void)flags;
    return 0;
}

int64_t handle_fchown(int fd, uid_t owner, gid_t group) {
    (void)fd; (void)owner; (void)group;
    return 0;
}

int64_t handle_utimensat(int dirfd, const char *pathname, const struct linux_timespec times[2], int flags) {
    char host_path[1024];
    const char *target = pathname;
    if (pathname && pathname[0] != '\0') {
        int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0);
        if (res < 0) return res;
        if (res == 1) return 0; /* virtual path */
        target = host_path;
        dirfd = AT_FDCWD;
    } else {
        target = NULL;
    }

    struct timespec dtimes[2];
    struct timespec *ptimes = NULL;
    if (times) {
        for (int i = 0; i < 2; i++) {
            dtimes[i].tv_sec = times[i].tv_sec;
            if (times[i].tv_nsec == 1073741823L) { /* Linux UTIME_NOW (0x3fffffff) */
                dtimes[i].tv_nsec = UTIME_NOW;     /* Darwin UTIME_NOW (-1) */
            } else if (times[i].tv_nsec == 1073741822L) { /* Linux UTIME_OMIT (0x3ffffffe) */
                dtimes[i].tv_nsec = UTIME_OMIT;   /* Darwin UTIME_OMIT (-2) */
            } else {
                dtimes[i].tv_nsec = times[i].tv_nsec;
            }
        }
        ptimes = dtimes;
    }

    int dflags = 0;
    if (flags & LINUX_AT_SYMLINK_NOFOLLOW) {
        dflags |= AT_SYMLINK_NOFOLLOW;
    }

    int ret = utimensat(dirfd, target, ptimes, dflags);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_sched_getaffinity(pid_t pid, size_t cpusetsize, unsigned long *mask) {
    (void)pid;
    if (!mask || cpusetsize < sizeof(unsigned long)) return -LINUX_EINVAL;
    int ncpu = 8;
    size_t size = sizeof(ncpu);
    sysctlbyname("hw.ncpu", &ncpu, &size, NULL, 0);

    memset(mask, 0, cpusetsize);
    for (int i = 0; i < ncpu && i < (int)(cpusetsize * 8); i++) {
        mask[i / 64] |= (1ULL << (i % 64));
    }
    return sizeof(unsigned long);
}

int64_t handle_sched_setaffinity(pid_t pid, size_t cpusetsize, const unsigned long *mask) {
    (void)pid; (void)cpusetsize; (void)mask;
    return 0;
}

int64_t handle_socket(int domain, int type, int protocol) {
    int ddomain = domain;
    if (domain == 1) ddomain = AF_UNIX;
    else if (domain == 2) ddomain = AF_INET;
    else if (domain == 10) ddomain = AF_INET6;
    else if (domain == 16) {
        return -LINUX_EAFNOSUPPORT;
    }

    int dtype = type & 0xf;
    int cloexec = (type & 02000000) != 0;
    int nonblock = (type & 00004000) != 0;

    int fd = socket(ddomain, dtype, protocol);
    if (fd < 0) return darwin_to_linux_errno(errno);

    if (cloexec) fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (nonblock) {
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    return fd;
}

int64_t handle_connect(int fd, const void *addr, socklen_t len) {
    int ret = connect(fd, (const struct sockaddr *)addr, len);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_bind(int fd, const void *addr, socklen_t len) {
    int ret = bind(fd, (const struct sockaddr *)addr, len);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_listen(int fd, int backlog) {
    int ret = listen(fd, backlog);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_accept(int fd, void *addr, socklen_t *len) {
    int ret = accept(fd, (struct sockaddr *)addr, len);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_getsockname(int fd, void *addr, socklen_t *len) {
    int ret = getsockname(fd, (struct sockaddr *)addr, len);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_getpeername(int fd, void *addr, socklen_t *len) {
    int ret = getpeername(fd, (struct sockaddr *)addr, len);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_sendto(int fd, const void *buf, size_t len, int flags, const void *dest_addr, socklen_t addrlen) {
    ssize_t ret = sendto(fd, buf, len, flags, (const struct sockaddr *)dest_addr, addrlen);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_recvfrom(int fd, void *buf, size_t len, int flags, void *src_addr, socklen_t *addrlen) {
    ssize_t ret = recvfrom(fd, buf, len, flags, (struct sockaddr *)src_addr, addrlen);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen) {
    int ret = setsockopt(fd, level, optname, optval, optlen);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen) {
    int ret = getsockopt(fd, level, optname, optval, optlen);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_shutdown(int fd, int how) {
    int ret = shutdown(fd, how);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

