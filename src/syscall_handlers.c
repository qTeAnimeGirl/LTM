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
#include <sys/select.h>
#include <sys/file.h>
#include <dispatch/dispatch.h>
#include <sys/xattr.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <stdatomic.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <net/route.h>
#include <sys/sysctl.h>
#include <setjmp.h>

#define LTM_MAX_THREADS 256

typedef struct {
    int tid;
    pthread_t pthread;
    _Atomic int active;
} ltm_thread_record_t;

static ltm_thread_record_t g_thread_table[LTM_MAX_THREADS];
static pthread_mutex_t g_thread_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int g_next_thread_tid = 0;
static _Atomic int g_active_thread_count = 1;

static __thread uintptr_t s_guest_tls = 0;
static __thread int *s_child_tid_ptr = NULL;
static __thread int s_thread_tid = 0;
static __thread int s_is_main_thread = 1;
static __thread sigjmp_buf s_thread_exit_jmp;
static __thread bool s_has_exit_jmp = false;
static __thread int s_crash_depth = 0;

static void ltm_reset_on_fork(void);

uintptr_t ltm_get_guest_tls(void) {
    return s_guest_tls;
}

void ltm_set_guest_tls(uintptr_t val) {
    s_guest_tls = val;
    __asm__ volatile("msr tpidr_el0, %0" : : "r"(val));
}

int *ltm_get_child_tid_ptr(void) {
    return s_child_tid_ptr;
}

void ltm_set_child_tid_ptr(int *ptr) {
    s_child_tid_ptr = ptr;
    g_ltm.child_tid_ptr = ptr;
}

int64_t ltm_get_thread_tid(void) {
    if (s_thread_tid > 0) return s_thread_tid;
    return getpid();
}

void ltm_set_thread_tid(int tid) {
    s_thread_tid = tid;
}

static int ltm_allocate_tid(void) {
    int expected = 0;
    atomic_compare_exchange_strong(&g_next_thread_tid, &expected, getpid() + 1);
    return atomic_fetch_add(&g_next_thread_tid, 1);
}

/* Futex bucket structure */
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    bool initialized;
} futex_bucket_t;

#define FUTEX_BUCKETS 256
static futex_bucket_t g_futex_table[FUTEX_BUCKETS];
static pthread_once_t g_futex_init_once = PTHREAD_ONCE_INIT;

static void init_all_futex_buckets(void) {
    for (int i = 0; i < FUTEX_BUCKETS; i++) {
        pthread_mutex_init(&g_futex_table[i].mutex, NULL);
        pthread_cond_init(&g_futex_table[i].cond, NULL);
        g_futex_table[i].initialized = true;
    }
}

static inline futex_bucket_t *get_futex_bucket(uint32_t *uaddr) {
    pthread_once(&g_futex_init_once, init_all_futex_buckets);
    uint32_t bucket_idx = ((uintptr_t)uaddr >> 2) % FUTEX_BUCKETS;
    return &g_futex_table[bucket_idx];
}

/* Directory descriptor cache for getdents64 */
#define DIR_CACHE_MAX 256
static DIR *g_dir_cache[DIR_CACHE_MAX];

/* Track ICMP sockets to strip IPv4 header on receive & SO_TIMESTAMP emulation */
#define MAX_TRACKED_FDS 1024
static bool g_sock_is_icmp[MAX_TRACKED_FDS];
static bool g_sock_so_timestamp[MAX_TRACKED_FDS];
static struct timeval g_sock_last_timestamp[MAX_TRACKED_FDS];

/* Eventfd Emulation */
typedef struct {
    int read_fd;
    int write_fd;
    int flags;
    bool in_use;
} eventfd_entry_t;

#define MAX_EVENTFDS 64
static eventfd_entry_t g_eventfds[MAX_EVENTFDS];
static pthread_mutex_t g_eventfd_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ltm_eventfd_close(int fd) {
    if (fd < 0) return;
    pthread_mutex_lock(&g_eventfd_mutex);
    for (int i = 0; i < MAX_EVENTFDS; i++) {
        if (g_eventfds[i].in_use && g_eventfds[i].read_fd == fd) {
            if (g_eventfds[i].write_fd >= 0) {
                close(g_eventfds[i].write_fd);
                g_eventfds[i].write_fd = -1;
            }
            g_eventfds[i].in_use = false;
            break;
        }
    }
    pthread_mutex_unlock(&g_eventfd_mutex);
}

static int ltm_eventfd_get_write_fd(int fd) {
    if (fd < 0) return -1;
    int wfd = -1;
    pthread_mutex_lock(&g_eventfd_mutex);
    for (int i = 0; i < MAX_EVENTFDS; i++) {
        if (g_eventfds[i].in_use && g_eventfds[i].read_fd == fd) {
            wfd = g_eventfds[i].write_fd;
            break;
        }
    }
    pthread_mutex_unlock(&g_eventfd_mutex);
    return wfd;
}

static bool ltm_eventfd_is_eventfd(int fd) {
    return ltm_eventfd_get_write_fd(fd) >= 0;
}

bool ltm_netlink_is_netlink(int fd);
void ltm_netlink_close(int fd);
ssize_t ltm_netlink_handle_send(int fd, const void *buf, size_t len);

/* File I/O */
int64_t handle_read(int fd, void *buf, size_t count) {
    if (count > 0 && !buf) return -LINUX_EFAULT;
    if (ltm_netlink_is_netlink(fd)) {
        ssize_t r = recv(fd, buf, count, 0);
        if (r < 0) return darwin_to_linux_errno(errno);
        return r;
    }
    if (ltm_eventfd_is_eventfd(fd)) {
        if (count < sizeof(uint64_t)) return -LINUX_EINVAL;
    }
    ssize_t ret = read(fd, buf, count);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_write(int fd, const void *buf, size_t count) {
    if (count > 0 && !buf) return -LINUX_EFAULT;
    if (ltm_netlink_is_netlink(fd)) {
        return ltm_netlink_handle_send(fd, buf, count);
    }
    int ev_wfd = ltm_eventfd_get_write_fd(fd);
    if (ev_wfd >= 0) {
        if (count < sizeof(uint64_t)) return -LINUX_EINVAL;
        uint64_t val = *(const uint64_t *)buf;
        if (val == 0xffffffffffffffffULL) return -LINUX_EINVAL;
        ssize_t ret = write(ev_wfd, &val, sizeof(val));
        if (ret < 0) return darwin_to_linux_errno(errno);
        return sizeof(uint64_t);
    }
    ssize_t ret = write(fd, buf, count);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_openat(int dirfd, const char *pathname, int flags, mode_t mode) {
    if (!pathname) return -LINUX_EFAULT;

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
        return sandbox_open_virtual(host_path, dflags, mode);
    }

    int fd = open(host_path, dflags, mode);
    if (fd < 0) return darwin_to_linux_errno(errno);
    return fd;
}

void ltm_epoll_close(int fd);

/* Timerfd and Signalfd Emulation */
typedef struct {
    int read_fd;
    int write_fd;
    int clockid;
    struct linux_itimerspec spec;
    int64_t arm_time_ns;
    int64_t delay_ns;
    dispatch_source_t timer;
    bool active;
    bool in_use;
} timerfd_entry_t;

#define MAX_TIMERFDS 64
static timerfd_entry_t g_timerfds[MAX_TIMERFDS];
static pthread_mutex_t g_timerfd_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ltm_timerfd_close(int fd) {
    if (fd < 0) return;
    pthread_mutex_lock(&g_timerfd_mutex);
    for (int i = 0; i < MAX_TIMERFDS; i++) {
        if (g_timerfds[i].in_use && g_timerfds[i].read_fd == fd) {
            if (g_timerfds[i].timer) {
                dispatch_source_cancel(g_timerfds[i].timer);
                dispatch_release(g_timerfds[i].timer);
                g_timerfds[i].timer = NULL;
            }
            if (g_timerfds[i].write_fd >= 0) {
                close(g_timerfds[i].write_fd);
                g_timerfds[i].write_fd = -1;
            }
            g_timerfds[i].in_use = false;
            g_timerfds[i].active = false;
            break;
        }
    }
    pthread_mutex_unlock(&g_timerfd_mutex);
}

typedef struct {
    int read_fd;
    int write_fd;
    uint64_t mask;
    bool in_use;
} signalfd_entry_t;

#define MAX_SIGNALFDS 64
static signalfd_entry_t g_signalfds[MAX_SIGNALFDS];
static pthread_mutex_t g_signalfd_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ltm_signalfd_close(int fd) {
    if (fd < 0) return;
    pthread_mutex_lock(&g_signalfd_mutex);
    for (int i = 0; i < MAX_SIGNALFDS; i++) {
        if (g_signalfds[i].in_use && g_signalfds[i].read_fd == fd) {
            if (g_signalfds[i].write_fd >= 0) {
                close(g_signalfds[i].write_fd);
                g_signalfds[i].write_fd = -1;
            }
            g_signalfds[i].in_use = false;
            break;
        }
    }
    pthread_mutex_unlock(&g_signalfd_mutex);
}

static bool signalfd_deliver_signal(int sig, siginfo_t *info) {
    if (sig <= 0 || sig > 64) return false;
    bool delivered = false;
    pthread_mutex_lock(&g_signalfd_mutex);
    for (int i = 0; i < MAX_SIGNALFDS; i++) {
        if (g_signalfds[i].in_use && (g_signalfds[i].mask & (1ULL << (sig - 1)))) {
            struct linux_signalfd_siginfo ssi;
            memset(&ssi, 0, sizeof(ssi));
            ssi.ssi_signo = (uint32_t)sig;
            ssi.ssi_errno = 0;
            ssi.ssi_code = info ? info->si_code : 0;
            ssi.ssi_pid = info ? (uint32_t)info->si_pid : (uint32_t)getpid();
            ssi.ssi_uid = info ? (uint32_t)info->si_uid : (uint32_t)getuid();
            write(g_signalfds[i].write_fd, &ssi, sizeof(ssi));
            delivered = true;
        }
    }
    pthread_mutex_unlock(&g_signalfd_mutex);
    return delivered;
}

void ltm_pidfd_close(int fd);
void ltm_mq_close(int fd);

int64_t handle_close(int fd) {
    if (fd >= 0 && fd < DIR_CACHE_MAX && g_dir_cache[fd]) {
        closedir(g_dir_cache[fd]);
        g_dir_cache[fd] = NULL;
    }
    if (fd >= 0 && fd < MAX_TRACKED_FDS) {
        g_sock_is_icmp[fd] = false;
        g_sock_so_timestamp[fd] = false;
        g_sock_last_timestamp[fd].tv_sec = 0;
        g_sock_last_timestamp[fd].tv_usec = 0;
    }
    ltm_epoll_close(fd);
    ltm_timerfd_close(fd);
    ltm_signalfd_close(fd);
    ltm_eventfd_close(fd);
    ltm_pidfd_close(fd);
    ltm_mq_close(fd);
    ltm_netlink_close(fd);
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
    statbuf->st_uid = 0;
    statbuf->st_gid = 0;
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
    if (!pathname) return -LINUX_EFAULT;

    if (pathname[0] == '\0') {
        if (!(flags & LINUX_AT_EMPTY_PATH)) return -LINUX_ENOENT;
        if (dirfd < 0) return -LINUX_EBADF;
        struct stat st;
        if (fstat(dirfd, &st) != 0) return darwin_to_linux_errno(errno);
        copy_stat_to_linux(&st, statbuf);
        return 0;
    }

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

    if (res == 1) {
        struct stat st;
        int vres = sandbox_virtual_stat(host_path, &st);
        if (vres < 0) return vres;
        copy_stat_to_linux(&st, statbuf);
        return 0;
    }

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

#define LINUX_ISIG     0x00001
#define LINUX_ICANON   0x00002
#define LINUX_ECHO     0x00008
#define LINUX_ECHOE    0x00010
#define LINUX_ECHOK    0x00020
#define LINUX_ECHONL   0x00040
#define LINUX_NOFLSH   0x00080
#define LINUX_TOSTOP   0x00100
#define LINUX_ECHOCTL  0x00200
#define LINUX_ECHOPRT  0x00400
#define LINUX_ECHOKE   0x00800
#define LINUX_FLUSHO   0x01000
#define LINUX_PENDIN   0x04000
#define LINUX_IEXTEN   0x08000
#define LINUX_EXTPROC  0x10000

#define LINUX_VINTR    0
#define LINUX_VQUIT    1
#define LINUX_VERASE   2
#define LINUX_VKILL    3
#define LINUX_VEOF     4
#define LINUX_VTIME    5
#define LINUX_VMIN     6
#define LINUX_VSTART   8
#define LINUX_VSTOP    9
#define LINUX_VSUSP    10
#define LINUX_VEOL     11
#define LINUX_VREPRINT 12
#define LINUX_VDISCARD 13
#define LINUX_VWERASE  14
#define LINUX_VLNEXT   15
#define LINUX_VEOL2    16

static void copy_darwin_to_linux_termios(const struct termios *d, struct linux_termios *l) {
    memset(l, 0, sizeof(*l));

    /* iflag */
    uint32_t l_iflag = 0;
    if (d->c_iflag & IGNBRK)  l_iflag |= 0x001;
    if (d->c_iflag & BRKINT)  l_iflag |= 0x002;
    if (d->c_iflag & IGNPAR)  l_iflag |= 0x004;
    if (d->c_iflag & PARMRK)  l_iflag |= 0x008;
    if (d->c_iflag & INPCK)   l_iflag |= 0x010;
    if (d->c_iflag & ISTRIP)  l_iflag |= 0x020;
    if (d->c_iflag & INLCR)   l_iflag |= 0x040;
    if (d->c_iflag & IGNCR)   l_iflag |= 0x080;
    if (d->c_iflag & ICRNL)   l_iflag |= 0x100;
    if (d->c_iflag & IXON)    l_iflag |= 0x400;
    if (d->c_iflag & IXANY)   l_iflag |= 0x800;
    if (d->c_iflag & IXOFF)   l_iflag |= 0x1000;
    if (d->c_iflag & IMAXBEL) l_iflag |= 0x2000;
    if (d->c_iflag & IUTF8)   l_iflag |= 0x4000;
    l->c_iflag = l_iflag;

    /* oflag */
    uint32_t l_oflag = 0;
    if (d->c_oflag & OPOST)  l_oflag |= 0x01;
    if (d->c_oflag & ONLCR)  l_oflag |= 0x04;
    if (d->c_oflag & OCRNL)  l_oflag |= 0x08;
    if (d->c_oflag & ONOCR)  l_oflag |= 0x10;
    if (d->c_oflag & ONLRET) l_oflag |= 0x20;
    if (d->c_oflag & OFILL)  l_oflag |= 0x40;
    if (d->c_oflag & OFDEL)  l_oflag |= 0x80;
    if (d->c_oflag & OXTABS) l_oflag |= 0x1800;
    l->c_oflag = l_oflag;

    /* cflag */
    uint32_t l_cflag = 0;
    tcflag_t csize = d->c_cflag & CSIZE;
    if (csize == CS5) l_cflag |= 0x00;
    else if (csize == CS6) l_cflag |= 0x10;
    else if (csize == CS7) l_cflag |= 0x20;
    else if (csize == CS8) l_cflag |= 0x30;

    if (d->c_cflag & CSTOPB)  l_cflag |= 0x40;
    if (d->c_cflag & CREAD)   l_cflag |= 0x80;
    if (d->c_cflag & PARENB)  l_cflag |= 0x100;
    if (d->c_cflag & PARODD)  l_cflag |= 0x200;
    if (d->c_cflag & HUPCL)   l_cflag |= 0x400;
    if (d->c_cflag & CLOCAL)  l_cflag |= 0x800;
    if (d->c_cflag & CRTSCTS) l_cflag |= 0x80000000;

    speed_t ospd = cfgetospeed(d);
    uint32_t cbaud = 0x000f;
    switch (ospd) {
        case B0: cbaud = 0x0000; break;
        case B50: cbaud = 0x0001; break;
        case B75: cbaud = 0x0002; break;
        case B110: cbaud = 0x0003; break;
        case B134: cbaud = 0x0004; break;
        case B150: cbaud = 0x0005; break;
        case B200: cbaud = 0x0006; break;
        case B300: cbaud = 0x0007; break;
        case B600: cbaud = 0x0008; break;
        case B1200: cbaud = 0x0009; break;
        case B1800: cbaud = 0x000a; break;
        case B2400: cbaud = 0x000b; break;
        case B4800: cbaud = 0x000c; break;
        case B9600: cbaud = 0x000d; break;
        case B19200: cbaud = 0x000e; break;
        case B38400: cbaud = 0x000f; break;
        case B57600: cbaud = 0x1001; break;
        case B115200: cbaud = 0x1002; break;
        case B230400: cbaud = 0x1003; break;
        default: cbaud = 0x000f; break;
    }
    l_cflag |= cbaud;
    l->c_cflag = l_cflag;

    /* lflag */
    uint32_t l_lflag = 0;
    if (d->c_lflag & ISIG)     l_lflag |= LINUX_ISIG;
    if (d->c_lflag & ICANON)   l_lflag |= LINUX_ICANON;
    if (d->c_lflag & ECHO)     l_lflag |= LINUX_ECHO;
    if (d->c_lflag & ECHOE)    l_lflag |= LINUX_ECHOE;
    if (d->c_lflag & ECHOK)    l_lflag |= LINUX_ECHOK;
    if (d->c_lflag & ECHONL)   l_lflag |= LINUX_ECHONL;
    if (d->c_lflag & NOFLSH)   l_lflag |= LINUX_NOFLSH;
    if (d->c_lflag & TOSTOP)   l_lflag |= LINUX_TOSTOP;
    if (d->c_lflag & ECHOCTL)  l_lflag |= LINUX_ECHOCTL;
    if (d->c_lflag & ECHOPRT)  l_lflag |= LINUX_ECHOPRT;
    if (d->c_lflag & ECHOKE)   l_lflag |= LINUX_ECHOKE;
    if (d->c_lflag & FLUSHO)   l_lflag |= LINUX_FLUSHO;
    if (d->c_lflag & PENDIN)   l_lflag |= LINUX_PENDIN;
    if (d->c_lflag & IEXTEN)   l_lflag |= LINUX_IEXTEN;
    if (d->c_lflag & EXTPROC)  l_lflag |= LINUX_EXTPROC;
    l->c_lflag = l_lflag;

    /* c_cc */
    l->c_cc[LINUX_VINTR]    = d->c_cc[VINTR];
    l->c_cc[LINUX_VQUIT]    = d->c_cc[VQUIT];
    l->c_cc[LINUX_VERASE]   = d->c_cc[VERASE];
    l->c_cc[LINUX_VKILL]    = d->c_cc[VKILL];
    l->c_cc[LINUX_VEOF]     = d->c_cc[VEOF];
    l->c_cc[LINUX_VTIME]    = d->c_cc[VTIME];
    l->c_cc[LINUX_VMIN]     = d->c_cc[VMIN];
    l->c_cc[LINUX_VSTART]   = d->c_cc[VSTART];
    l->c_cc[LINUX_VSTOP]    = d->c_cc[VSTOP];
    l->c_cc[LINUX_VSUSP]    = d->c_cc[VSUSP];
    l->c_cc[LINUX_VEOL]     = d->c_cc[VEOL];
    l->c_cc[LINUX_VREPRINT] = d->c_cc[VREPRINT];
    l->c_cc[LINUX_VDISCARD] = d->c_cc[VDISCARD];
    l->c_cc[LINUX_VWERASE]  = d->c_cc[VWERASE];
    l->c_cc[LINUX_VLNEXT]   = d->c_cc[VLNEXT];
    l->c_cc[LINUX_VEOL2]    = d->c_cc[VEOL2];
}

static void copy_linux_to_darwin_termios(const struct linux_termios *l, struct termios *d) {
    /* 1. iflag */
    tcflag_t d_iflag = 0;
    if (l->c_iflag & 0x001)  d_iflag |= IGNBRK;
    if (l->c_iflag & 0x002)  d_iflag |= BRKINT;
    if (l->c_iflag & 0x004)  d_iflag |= IGNPAR;
    if (l->c_iflag & 0x008)  d_iflag |= PARMRK;
    if (l->c_iflag & 0x010)  d_iflag |= INPCK;
    if (l->c_iflag & 0x020)  d_iflag |= ISTRIP;
    if (l->c_iflag & 0x040)  d_iflag |= INLCR;
    if (l->c_iflag & 0x080)  d_iflag |= IGNCR;
    if (l->c_iflag & 0x100)  d_iflag |= ICRNL;
    if (l->c_iflag & 0x400)  d_iflag |= IXON;
    if (l->c_iflag & 0x800)  d_iflag |= IXANY;
    if (l->c_iflag & 0x1000) d_iflag |= IXOFF;
    if (l->c_iflag & 0x2000) d_iflag |= IMAXBEL;
    if (l->c_iflag & 0x4000) d_iflag |= IUTF8;
    d->c_iflag = d_iflag;

    /* 2. oflag */
    tcflag_t d_oflag = 0;
    if (l->c_oflag & 0x01)   d_oflag |= OPOST;
    if (l->c_oflag & 0x04)   d_oflag |= ONLCR;
    if (l->c_oflag & 0x08)   d_oflag |= OCRNL;
    if (l->c_oflag & 0x10)   d_oflag |= ONOCR;
    if (l->c_oflag & 0x20)   d_oflag |= ONLRET;
    if (l->c_oflag & 0x40)   d_oflag |= OFILL;
    if (l->c_oflag & 0x80)   d_oflag |= OFDEL;
    if ((l->c_oflag & 0x1800) == 0x1800) d_oflag |= OXTABS;
    d->c_oflag = d_oflag;

    /* 3. cflag */
    tcflag_t d_cflag = 0;
    uint32_t csize = l->c_cflag & 0x30;
    if (csize == 0x00) d_cflag |= CS5;
    else if (csize == 0x10) d_cflag |= CS6;
    else if (csize == 0x20) d_cflag |= CS7;
    else if (csize == 0x30) d_cflag |= CS8;

    if (l->c_cflag & 0x40)       d_cflag |= CSTOPB;
    if (l->c_cflag & 0x80)       d_cflag |= CREAD;
    if (l->c_cflag & 0x100)      d_cflag |= PARENB;
    if (l->c_cflag & 0x200)      d_cflag |= PARODD;
    if (l->c_cflag & 0x400)      d_cflag |= HUPCL;
    if (l->c_cflag & 0x800)      d_cflag |= CLOCAL;
    if (l->c_cflag & 0x80000000) d_cflag |= CRTSCTS;
    d->c_cflag = d_cflag;

    /* 4. lflag */
    tcflag_t d_lflag = 0;
    if (l->c_lflag & LINUX_ISIG)     d_lflag |= ISIG;
    if (l->c_lflag & LINUX_ICANON)   d_lflag |= ICANON;
    if (l->c_lflag & LINUX_ECHO)     d_lflag |= ECHO;
    if (l->c_lflag & LINUX_ECHOE)    d_lflag |= ECHOE;
    if (l->c_lflag & LINUX_ECHOK)    d_lflag |= ECHOK;
    if (l->c_lflag & LINUX_ECHONL)   d_lflag |= ECHONL;
    if (l->c_lflag & LINUX_NOFLSH)   d_lflag |= NOFLSH;
    if (l->c_lflag & LINUX_TOSTOP)   d_lflag |= TOSTOP;
    if (l->c_lflag & LINUX_ECHOCTL)  d_lflag |= ECHOCTL;
    if (l->c_lflag & LINUX_ECHOPRT)  d_lflag |= ECHOPRT;
    if (l->c_lflag & LINUX_ECHOKE)   d_lflag |= ECHOKE;
    if (l->c_lflag & LINUX_FLUSHO)   d_lflag |= FLUSHO;
    if (l->c_lflag & LINUX_PENDIN)   d_lflag |= PENDIN;
    if (l->c_lflag & LINUX_IEXTEN)   d_lflag |= IEXTEN;
    if (l->c_lflag & LINUX_EXTPROC)  d_lflag |= EXTPROC;
    d->c_lflag = d_lflag;

    /* 5. c_cc */
    d->c_cc[VEOF]     = l->c_cc[LINUX_VEOF];
    d->c_cc[VEOL]     = l->c_cc[LINUX_VEOL];
    d->c_cc[VEOL2]    = l->c_cc[LINUX_VEOL2];
    d->c_cc[VERASE]   = l->c_cc[LINUX_VERASE];
    d->c_cc[VWERASE]  = l->c_cc[LINUX_VWERASE];
    d->c_cc[VKILL]    = l->c_cc[LINUX_VKILL];
    d->c_cc[VREPRINT] = l->c_cc[LINUX_VREPRINT];
    d->c_cc[VINTR]    = l->c_cc[LINUX_VINTR];
    d->c_cc[VQUIT]    = l->c_cc[LINUX_VQUIT];
    d->c_cc[VSUSP]    = l->c_cc[LINUX_VSUSP];
    d->c_cc[VSTART]   = l->c_cc[LINUX_VSTART];
    d->c_cc[VSTOP]    = l->c_cc[LINUX_VSTOP];
    d->c_cc[VLNEXT]   = l->c_cc[LINUX_VLNEXT];
    d->c_cc[VDISCARD] = l->c_cc[LINUX_VDISCARD];
    d->c_cc[VMIN]     = l->c_cc[LINUX_VMIN];
    d->c_cc[VTIME]    = l->c_cc[LINUX_VTIME];

    /* 6. Baud rate */
    uint32_t cbaud = l->c_cflag & 0x100f;
    speed_t spd = B38400;
    switch (cbaud) {
        case 0x0000: spd = B0; break;
        case 0x0001: spd = B50; break;
        case 0x0002: spd = B75; break;
        case 0x0003: spd = B110; break;
        case 0x0004: spd = B134; break;
        case 0x0005: spd = B150; break;
        case 0x0006: spd = B200; break;
        case 0x0007: spd = B300; break;
        case 0x0008: spd = B600; break;
        case 0x0009: spd = B1200; break;
        case 0x000a: spd = B1800; break;
        case 0x000b: spd = B2400; break;
        case 0x000c: spd = B4800; break;
        case 0x000d: spd = B9600; break;
        case 0x000e: spd = B19200; break;
        case 0x000f: spd = B38400; break;
        case 0x1001: spd = B57600; break;
        case 0x1002: spd = B115200; break;
        case 0x1003: spd = B230400; break;
        default: spd = B38400; break;
    }
    cfsetispeed(d, spd);
    cfsetospeed(d, spd);
}

static void copy_darwin_to_linux_termios2(const struct termios *d, struct linux_termios2 *l2) {
    struct linux_termios l;
    copy_darwin_to_linux_termios(d, &l);
    memcpy(l2, &l, sizeof(l));
    l2->c_ispeed = (uint32_t)cfgetispeed(d);
    l2->c_ospeed = (uint32_t)cfgetospeed(d);
}

static void copy_linux_to_darwin_termios2(const struct linux_termios2 *l2, struct termios *d) {
    struct linux_termios l;
    memcpy(&l, l2, sizeof(l));
    copy_linux_to_darwin_termios(&l, d);
    if (l2->c_ispeed) cfsetispeed(d, (speed_t)l2->c_ispeed);
    if (l2->c_ospeed) cfsetospeed(d, (speed_t)l2->c_ospeed);
}

typedef struct {
    char linux_name[LINUX_IFNAMSIZ];
    char darwin_name[32];
    int ifindex;
    short flags;          /* Linux flags */
    uint32_t mtu;
    bool has_ipv4;
    struct in_addr ip;
    struct in_addr netmask;
    struct in_addr broadcast;
    bool has_mac;
    unsigned char mac[6];
    uint16_t arphdr_type;
    uint64_t rx_bytes;
    uint64_t rx_packets;
    uint64_t rx_errors;
    uint64_t rx_dropped;
    uint64_t tx_bytes;
    uint64_t tx_packets;
    uint64_t tx_errors;
    uint64_t tx_dropped;
    uint64_t collisions;
    uint64_t multicast;
} ltm_netif_info_t;

static void linux_to_darwin_ifname(const char *linux_name, char *darwin_name, size_t maxlen) {
    if (!linux_name || !darwin_name || maxlen == 0) return;
    if (strcmp(linux_name, "lo") == 0) {
        snprintf(darwin_name, maxlen, "lo0");
        return;
    }
    if (strncmp(linux_name, "eth", 3) == 0 && isdigit((unsigned char)linux_name[3])) {
        snprintf(darwin_name, maxlen, "en%s", linux_name + 3);
        return;
    }
    snprintf(darwin_name, maxlen, "%s", linux_name);
}

static void darwin_to_linux_ifname(const char *darwin_name, char *linux_name, size_t maxlen) {
    if (!darwin_name || !linux_name || maxlen == 0) return;
    if (strcmp(darwin_name, "lo0") == 0) {
        snprintf(linux_name, maxlen, "lo");
        return;
    }
    if (strncmp(darwin_name, "en", 2) == 0 && isdigit((unsigned char)darwin_name[2])) {
        snprintf(linux_name, maxlen, "eth%s", darwin_name + 2);
        return;
    }
    snprintf(linux_name, maxlen, "%s", darwin_name);
}

static short darwin_to_linux_if_flags(unsigned int dflags) {
    short lflags = 0;
    if (dflags & IFF_UP)          lflags |= LINUX_IFF_UP;
    if (dflags & IFF_BROADCAST)   lflags |= LINUX_IFF_BROADCAST;
    if (dflags & IFF_DEBUG)       lflags |= LINUX_IFF_DEBUG;
    if (dflags & IFF_LOOPBACK)    lflags |= LINUX_IFF_LOOPBACK;
    if (dflags & IFF_POINTOPOINT) lflags |= LINUX_IFF_POINTOPOINT;
    if (dflags & IFF_RUNNING)     lflags |= LINUX_IFF_RUNNING;
    if (dflags & IFF_NOARP)       lflags |= LINUX_IFF_NOARP;
    if (dflags & IFF_PROMISC)     lflags |= LINUX_IFF_PROMISC;
    if (dflags & IFF_ALLMULTI)    lflags |= LINUX_IFF_ALLMULTI;
    if (dflags & 0x8000 /* Darwin IFF_MULTICAST */) lflags |= LINUX_IFF_MULTICAST;
    return lflags;
}

static int ltm_collect_netifs(ltm_netif_info_t *out, int max_entries) {
    struct ifaddrs *ifap = NULL;
    if (getifaddrs(&ifap) != 0) return 0;

    int count = 0;

    for (struct ifaddrs *ifa = ifap; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_name) continue;

        char lname[LINUX_IFNAMSIZ];
        darwin_to_linux_ifname(ifa->ifa_name, lname, sizeof(lname));

        bool is_lo = (strcmp(ifa->ifa_name, "lo0") == 0);
        bool is_en = (strncmp(ifa->ifa_name, "en", 2) == 0 && isdigit((unsigned char)ifa->ifa_name[2]));
        if (!is_lo && !is_en) continue;

        int idx = -1;
        for (int i = 0; i < count; i++) {
            if (strcmp(out[i].linux_name, lname) == 0) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            if (count >= max_entries) continue;
            idx = count++;
            memset(&out[idx], 0, sizeof(ltm_netif_info_t));
            strncpy(out[idx].linux_name, lname, sizeof(out[idx].linux_name) - 1);
            strncpy(out[idx].darwin_name, ifa->ifa_name, sizeof(out[idx].darwin_name) - 1);
            out[idx].ifindex = count;
            if (is_lo) {
                out[idx].arphdr_type = LINUX_ARPHRD_LOOPBACK;
                out[idx].mtu = 65536;
            } else {
                out[idx].arphdr_type = LINUX_ARPHRD_ETHER;
                out[idx].mtu = 1500;
            }
        }

        if (ifa->ifa_flags) {
            out[idx].flags = darwin_to_linux_if_flags(ifa->ifa_flags);
        }

        if (ifa->ifa_addr) {
            if (ifa->ifa_addr->sa_family == AF_INET) {
                struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
                out[idx].has_ipv4 = true;
                out[idx].ip = sin->sin_addr;
                if (ifa->ifa_netmask) {
                    struct sockaddr_in *m = (struct sockaddr_in *)ifa->ifa_netmask;
                    out[idx].netmask = m->sin_addr;
                }
                if (ifa->ifa_dstaddr) {
                    struct sockaddr_in *b = (struct sockaddr_in *)ifa->ifa_dstaddr;
                    out[idx].broadcast = b->sin_addr;
                } else if (out[idx].netmask.s_addr != 0) {
                    out[idx].broadcast.s_addr = out[idx].ip.s_addr | ~out[idx].netmask.s_addr;
                }
            } else if (ifa->ifa_addr->sa_family == AF_LINK) {
                struct sockaddr_dl *sdl = (struct sockaddr_dl *)ifa->ifa_addr;
                struct if_data *ifd = (struct if_data *)ifa->ifa_data;
                if (sdl && sdl->sdl_alen == 6) {
                    out[idx].has_mac = true;
                    memcpy(out[idx].mac, LLADDR(sdl), 6);
                }
                if (ifd) {
                    if (ifd->ifi_mtu > 0) out[idx].mtu = ifd->ifi_mtu;
                    out[idx].rx_bytes = ifd->ifi_ibytes;
                    out[idx].rx_packets = ifd->ifi_ipackets;
                    out[idx].rx_errors = ifd->ifi_ierrors;
                    out[idx].rx_dropped = ifd->ifi_iqdrops;
                    out[idx].tx_bytes = ifd->ifi_obytes;
                    out[idx].tx_packets = ifd->ifi_opackets;
                    out[idx].tx_errors = ifd->ifi_oerrors;
                    out[idx].collisions = ifd->ifi_collisions;
                    out[idx].multicast = ifd->ifi_imcasts;
                }
            }
        }
    }

    freeifaddrs(ifap);

    for (int i = 1; i < count; i++) {
        if (strcmp(out[i].linux_name, "lo") == 0) {
            ltm_netif_info_t tmp = out[0];
            out[0] = out[i];
            out[i] = tmp;
            out[0].ifindex = 1;
            out[i].ifindex = i + 1;
            break;
        }
    }
    return count;
}

int64_t handle_ioctl(int fd, unsigned long request, void *arg) {
    /* TIOCGWINSZ: Linux 0x5413 */
    if (request == 0x5413) {
        if (!arg) return -LINUX_EFAULT;
        struct winsize ws;
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0) {
            struct linux_winsize *lws = (struct linux_winsize *)arg;
            lws->ws_row = ws.ws_row;
            lws->ws_col = ws.ws_col;
            lws->ws_xpixel = ws.ws_xpixel;
            lws->ws_ypixel = ws.ws_ypixel;
            return 0;
        }
        return darwin_to_linux_errno(errno);
    }

    /* TIOCSWINSZ: Linux 0x5414 */
    if (request == 0x5414) {
        if (!arg) return -LINUX_EFAULT;
        struct linux_winsize *lws = (struct linux_winsize *)arg;
        struct winsize ws;
        ws.ws_row = lws->ws_row;
        ws.ws_col = lws->ws_col;
        ws.ws_xpixel = lws->ws_xpixel;
        ws.ws_ypixel = lws->ws_ypixel;
        if (ioctl(fd, TIOCSWINSZ, &ws) == 0) return 0;
        return darwin_to_linux_errno(errno);
    }

    /* TCGETS: Linux 0x5401 */
    if (request == 0x5401) {
        if (!arg) return -LINUX_EFAULT;
        struct termios tios;
        if (tcgetattr(fd, &tios) != 0) {
            return darwin_to_linux_errno(errno);
        }
        copy_darwin_to_linux_termios(&tios, (struct linux_termios *)arg);
        return 0;
    }

    /* TCGETS2: Linux 0x802c542a */
    if (request == 0x802c542a) {
        if (!arg) return -LINUX_EFAULT;
        struct termios tios;
        if (tcgetattr(fd, &tios) != 0) {
            return darwin_to_linux_errno(errno);
        }
        copy_darwin_to_linux_termios2(&tios, (struct linux_termios2 *)arg);
        return 0;
    }

    /* TCSETS: Linux 0x5402, TCSETSW: 0x5403, TCSETSF: 0x5404 */
    if (request == 0x5402 || request == 0x5403 || request == 0x5404) {
        if (!arg) return -LINUX_EFAULT;
        struct termios tios;
        if (tcgetattr(fd, &tios) != 0) {
            memset(&tios, 0, sizeof(tios));
        }
        copy_linux_to_darwin_termios((const struct linux_termios *)arg, &tios);
        int opt = TCSANOW;
        if (request == 0x5403) opt = TCSADRAIN;
        else if (request == 0x5404) opt = TCSAFLUSH;
        if (tcsetattr(fd, opt, &tios) != 0) {
            return darwin_to_linux_errno(errno);
        }
        return 0;
    }

    /* TCSETS2: Linux 0x402c542b, TCSETSW2: 0x402c542c, TCSETSF2: 0x402c542d */
    if (request == 0x402c542b || request == 0x402c542c || request == 0x402c542d) {
        if (!arg) return -LINUX_EFAULT;
        struct termios tios;
        if (tcgetattr(fd, &tios) != 0) {
            memset(&tios, 0, sizeof(tios));
        }
        copy_linux_to_darwin_termios2((const struct linux_termios2 *)arg, &tios);
        int opt = TCSANOW;
        if (request == 0x402c542c) opt = TCSADRAIN;
        else if (request == 0x402c542d) opt = TCSAFLUSH;
        if (tcsetattr(fd, opt, &tios) != 0) {
            return darwin_to_linux_errno(errno);
        }
        return 0;
    }

    /* TCFLSH: Linux 0x540b */
    if (request == 0x540b) {
        int action = (int)(uintptr_t)arg;
        int d_action = TCIOFLUSH;
        if (action == 0) d_action = TCIFLUSH;
        else if (action == 1) d_action = TCOFLUSH;
        else if (action == 2) d_action = TCIOFLUSH;
        if (tcflush(fd, d_action) != 0) return darwin_to_linux_errno(errno);
        return 0;
    }

    /* TCXONC: Linux 0x540a */
    if (request == 0x540a) {
        int action = (int)(uintptr_t)arg;
        if (tcflow(fd, action) != 0) return darwin_to_linux_errno(errno);
        return 0;
    }

    /* TIOCSCTTY: Linux 0x540e */
    if (request == 0x540e) {
        ioctl(fd, TIOCSCTTY, 0);
        return 0;
    }

    /* TIOCGPGRP: Linux 0x540f */
    if (request == 0x540f) {
        if (!arg) return -LINUX_EFAULT;
        pid_t pgrp = tcgetpgrp(fd);
        if (pgrp <= 0) {
            pgrp = getpgrp();
        }
        if (pgrp <= 0) {
            pgrp = getpid();
        }
        *(pid_t *)arg = pgrp;
        return 0;
    }

    /* TIOCSPGRP: Linux 0x5410 */
    if (request == 0x5410) {
        if (!arg) return -LINUX_EFAULT;
        pid_t pgrp = *(pid_t *)arg;

        struct sigaction sa, old_sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTTOU, &sa, &old_sa);

        tcsetpgrp(fd, pgrp);

        sigaction(SIGTTOU, &old_sa, NULL);
        return 0;
    }

    /* TIOCGSID: Linux 0x5429 */
    if (request == 0x5429) {
        if (!arg) return -LINUX_EFAULT;
        pid_t sid = tcgetsid(fd);
        if (sid < 0) sid = getsid(0);
        *(pid_t *)arg = sid;
        return 0;
    }

    /* FIONREAD: Linux 0x541b */
    if (request == 0x541b) {
        if (!arg) return -LINUX_EFAULT;
        int n = 0;
        if (ioctl(fd, FIONREAD, &n) != 0) return darwin_to_linux_errno(errno);
        *(int *)arg = n;
        return 0;
    }

    /* SIOCGSTAMP: Linux 0x8906 (get packet timestamp as struct timeval) */
    if (request == 0x8906) {
        if (!arg) return -LINUX_EFAULT;
        struct timeval *tv = (struct timeval *)arg;
        if (fd >= 0 && fd < MAX_TRACKED_FDS && g_sock_last_timestamp[fd].tv_sec > 0) {
            *tv = g_sock_last_timestamp[fd];
        } else {
            gettimeofday(tv, NULL);
        }
        return 0;
    }

    /* SIOCGSTAMPNS: Linux 0x8907 (get packet timestamp as struct timespec) */
    if (request == 0x8907) {
        if (!arg) return -LINUX_EFAULT;
        struct timespec *ts = (struct timespec *)arg;
        if (fd >= 0 && fd < MAX_TRACKED_FDS && g_sock_last_timestamp[fd].tv_sec > 0) {
            ts->tv_sec = g_sock_last_timestamp[fd].tv_sec;
            ts->tv_nsec = (long)g_sock_last_timestamp[fd].tv_usec * 1000L;
        } else {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            ts->tv_sec = tv.tv_sec;
            ts->tv_nsec = (long)tv.tv_usec * 1000L;
        }
        return 0;
    }

    /* SIOCGIFCONF: Linux 0x8912 */
    if (request == LINUX_SIOCGIFCONF) {
        if (!arg) return -LINUX_EFAULT;
        struct linux_ifconf *ifc = (struct linux_ifconf *)arg;

        ltm_netif_info_t ifaces[16];
        int count = ltm_collect_netifs(ifaces, 16);

        int ipv4_count = 0;
        for (int i = 0; i < count; i++) {
            if (ifaces[i].has_ipv4) ipv4_count++;
        }

        if (!ifc->ifc_ifcu.ifcu_req || ifc->ifc_len <= 0) {
            ifc->ifc_len = (int)(ipv4_count * sizeof(struct linux_ifreq));
            return 0;
        }

        int max_entries = ifc->ifc_len / (int)sizeof(struct linux_ifreq);
        struct linux_ifreq *reqs = ifc->ifc_ifcu.ifcu_req;
        int written = 0;

        for (int i = 0; i < count && written < max_entries; i++) {
            if (!ifaces[i].has_ipv4) continue;
            memset(&reqs[written], 0, sizeof(struct linux_ifreq));
            strncpy(reqs[written].ifr_name, ifaces[i].linux_name, LINUX_IFNAMSIZ - 1);
            reqs[written].ifr_ifru.ifru_addr.sa_family = 2; /* AF_INET */
            memcpy(&reqs[written].ifr_ifru.ifru_addr.sa_data[2], &ifaces[i].ip, 4);
            written++;
        }

        ifc->ifc_len = (int)(written * sizeof(struct linux_ifreq));
        return 0;
    }

    /* Single-interface ioctls: SIOCGIFFLAGS, SIOCGIFADDR, SIOCGIFNETMASK, SIOCGIFBRDADDR,
     * SIOCGIFHWADDR, SIOCGIFMTU, SIOCGIFINDEX, SIOCGIFNAME, SIOCGIFTXQLEN, SIOCGIFMETRIC */
    if (request == LINUX_SIOCGIFFLAGS || request == LINUX_SIOCSIFFLAGS ||
        request == LINUX_SIOCGIFADDR   || request == LINUX_SIOCSIFADDR   ||
        request == LINUX_SIOCGIFNETMASK|| request == LINUX_SIOCSIFNETMASK||
        request == LINUX_SIOCGIFBRDADDR|| request == LINUX_SIOCSIFBRDADDR||
        request == LINUX_SIOCGIFHWADDR || request == LINUX_SIOCGIFMTU    ||
        request == LINUX_SIOCSIFMTU    || request == LINUX_SIOCGIFINDEX  ||
        request == LINUX_SIOCGIFNAME   || request == LINUX_SIOCGIFTXQLEN ||
        request == LINUX_SIOCSIFTXQLEN || request == LINUX_SIOCGIFMETRIC) {

        if (!arg) return -LINUX_EFAULT;
        struct linux_ifreq *ifr = (struct linux_ifreq *)arg;

        ltm_netif_info_t ifaces[16];
        int count = ltm_collect_netifs(ifaces, 16);

        int match_idx = -1;
        if (request == LINUX_SIOCGIFNAME) {
            int target_idx = ifr->ifr_ifru.ifru_ivalue;
            for (int i = 0; i < count; i++) {
                if (ifaces[i].ifindex == target_idx) {
                    match_idx = i;
                    break;
                }
            }
        } else {
            char query_darwin[LINUX_IFNAMSIZ];
            linux_to_darwin_ifname(ifr->ifr_name, query_darwin, sizeof(query_darwin));

            for (int i = 0; i < count; i++) {
                if (strcmp(ifaces[i].linux_name, ifr->ifr_name) == 0 ||
                    strcmp(ifaces[i].darwin_name, ifr->ifr_name) == 0 ||
                    strcmp(ifaces[i].darwin_name, query_darwin) == 0) {
                    match_idx = i;
                    break;
                }
            }
        }

        if (match_idx < 0) {
            return -LINUX_ENODEV;
        }

        ltm_netif_info_t *it = &ifaces[match_idx];

        switch (request) {
            case LINUX_SIOCGIFNAME:
                strncpy(ifr->ifr_name, it->linux_name, LINUX_IFNAMSIZ - 1);
                ifr->ifr_name[LINUX_IFNAMSIZ - 1] = '\0';
                return 0;
            case LINUX_SIOCGIFFLAGS:
                ifr->ifr_ifru.ifru_flags = it->flags;
                return 0;
            case LINUX_SIOCSIFFLAGS:
                return 0;
            case LINUX_SIOCGIFADDR:
                if (!it->has_ipv4) return -LINUX_EADDRNOTAVAIL;
                memset(&ifr->ifr_ifru.ifru_addr, 0, sizeof(ifr->ifr_ifru.ifru_addr));
                ifr->ifr_ifru.ifru_addr.sa_family = 2; /* AF_INET */
                memcpy(&ifr->ifr_ifru.ifru_addr.sa_data[2], &it->ip, 4);
                return 0;
            case LINUX_SIOCSIFADDR:
                return 0;
            case LINUX_SIOCGIFNETMASK:
                memset(&ifr->ifr_ifru.ifru_netmask, 0, sizeof(ifr->ifr_ifru.ifru_netmask));
                ifr->ifr_ifru.ifru_netmask.sa_family = 2;
                memcpy(&ifr->ifr_ifru.ifru_netmask.sa_data[2], &it->netmask, 4);
                return 0;
            case LINUX_SIOCSIFNETMASK:
                return 0;
            case LINUX_SIOCGIFBRDADDR:
                memset(&ifr->ifr_ifru.ifru_broadaddr, 0, sizeof(ifr->ifr_ifru.ifru_broadaddr));
                ifr->ifr_ifru.ifru_broadaddr.sa_family = 2;
                memcpy(&ifr->ifr_ifru.ifru_broadaddr.sa_data[2], &it->broadcast, 4);
                return 0;
            case LINUX_SIOCSIFBRDADDR:
                return 0;
            case LINUX_SIOCGIFHWADDR:
                memset(&ifr->ifr_ifru.ifru_hwaddr, 0, sizeof(ifr->ifr_ifru.ifru_hwaddr));
                ifr->ifr_ifru.ifru_hwaddr.sa_family = it->arphdr_type;
                memcpy(ifr->ifr_ifru.ifru_hwaddr.sa_data, it->mac, 6);
                return 0;
            case LINUX_SIOCGIFMTU:
                ifr->ifr_ifru.ifru_mtu = (int32_t)it->mtu;
                return 0;
            case LINUX_SIOCSIFMTU:
                return 0;
            case LINUX_SIOCGIFINDEX:
                ifr->ifr_ifru.ifru_ivalue = it->ifindex;
                return 0;
            case LINUX_SIOCGIFTXQLEN:
                ifr->ifr_ifru.ifru_ivalue = 1000;
                return 0;
            case LINUX_SIOCSIFTXQLEN:
                return 0;
            case LINUX_SIOCGIFMETRIC:
                ifr->ifr_ifru.ifru_ivalue = 0;
                return 0;
            default:
                return -LINUX_EINVAL;
        }
    }

    return -LINUX_ENOTTY;
}

int64_t handle_readv(int fd, const struct linux_iovec *iov, int iovcnt) {
    if (iovcnt == 0) return 0;
    if (iovcnt < 0 || iovcnt > 1024) return -LINUX_EINVAL;
    if (!iov) return -LINUX_EFAULT;

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
    if (iovcnt == 0) return 0;
    if (iovcnt < 0 || iovcnt > 1024) return -LINUX_EINVAL;
    if (!iov) return -LINUX_EFAULT;

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
    if (ret >= 0 && ret < MAX_TRACKED_FDS && oldfd >= 0 && oldfd < MAX_TRACKED_FDS) {
        g_sock_is_icmp[ret] = g_sock_is_icmp[oldfd];
        g_sock_so_timestamp[ret] = g_sock_so_timestamp[oldfd];
        g_sock_last_timestamp[ret] = g_sock_last_timestamp[oldfd];
    }
    return ret;
}

int64_t handle_dup3(int oldfd, int newfd, int flags) {
    if (oldfd == newfd) return -LINUX_EINVAL;
    int ret = dup2(oldfd, newfd);
    if (ret < 0) return darwin_to_linux_errno(errno);

    if (newfd >= 0 && newfd < MAX_TRACKED_FDS && oldfd >= 0 && oldfd < MAX_TRACKED_FDS) {
        g_sock_is_icmp[newfd] = g_sock_is_icmp[oldfd];
        g_sock_so_timestamp[newfd] = g_sock_so_timestamp[oldfd];
        g_sock_last_timestamp[newfd] = g_sock_last_timestamp[oldfd];
    }

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
    if (ret < 0) {
        int err = errno;
        if (err == 0 || err == ENOTTY) err = EINVAL;
        return darwin_to_linux_errno(err);
    }

    if ((cmd == F_DUPFD || cmd == 1030) && ret >= 0 && ret < MAX_TRACKED_FDS && fd >= 0 && fd < MAX_TRACKED_FDS) {
        g_sock_is_icmp[ret] = g_sock_is_icmp[fd];
        g_sock_so_timestamp[ret] = g_sock_so_timestamp[fd];
        g_sock_last_timestamp[ret] = g_sock_last_timestamp[fd];
    }

    return ret;
}

int64_t handle_getdents64(int fd, void *dirp, size_t count) {
    if (!dirp) return -LINUX_EFAULT;
    if (count == 0) return -LINUX_EINVAL;

    char dir_path[1024];
    bool is_proc = false;
    if (fcntl(fd, F_GETPATH, dir_path) == 0) {
        size_t dlen = strlen(dir_path);
        if ((dlen == 5 && strcmp(dir_path, "/proc") == 0) ||
            (dlen > 5 && strcmp(dir_path + dlen - 5, "/proc") == 0)) {
            is_proc = true;
        }
    }

    if (is_proc) {
        char pid_str[16];
        snprintf(pid_str, sizeof(pid_str), "%d", getpid());

        struct {
            const char *name;
            uint8_t d_type;
            uint64_t ino;
        } proc_entries[20];
        int total_entries = 0;

        proc_entries[total_entries].name = ".";
        proc_entries[total_entries].d_type = DT_DIR;
        proc_entries[total_entries++].ino = 1;

        proc_entries[total_entries].name = "..";
        proc_entries[total_entries].d_type = DT_DIR;
        proc_entries[total_entries++].ino = 1;

        proc_entries[total_entries].name = "1";
        proc_entries[total_entries].d_type = DT_DIR;
        proc_entries[total_entries++].ino = 2;

        if (getpid() != 1) {
            proc_entries[total_entries].name = pid_str;
            proc_entries[total_entries].d_type = DT_DIR;
            proc_entries[total_entries++].ino = 3;
        }

        proc_entries[total_entries].name = "self";
        proc_entries[total_entries].d_type = DT_LNK;
        proc_entries[total_entries++].ino = 4;

        proc_entries[total_entries].name = "cpuinfo";
        proc_entries[total_entries].d_type = DT_REG;
        proc_entries[total_entries++].ino = 5;

        proc_entries[total_entries].name = "meminfo";
        proc_entries[total_entries].d_type = DT_REG;
        proc_entries[total_entries++].ino = 6;

        proc_entries[total_entries].name = "version";
        proc_entries[total_entries].d_type = DT_REG;
        proc_entries[total_entries++].ino = 7;

        proc_entries[total_entries].name = "stat";
        proc_entries[total_entries].d_type = DT_REG;
        proc_entries[total_entries++].ino = 8;

        proc_entries[total_entries].name = "uptime";
        proc_entries[total_entries].d_type = DT_REG;
        proc_entries[total_entries++].ino = 9;

        proc_entries[total_entries].name = "loadavg";
        proc_entries[total_entries].d_type = DT_REG;
        proc_entries[total_entries++].ino = 10;

        proc_entries[total_entries].name = "mounts";
        proc_entries[total_entries].d_type = DT_REG;
        proc_entries[total_entries++].ino = 11;

        proc_entries[total_entries].name = "filesystems";
        proc_entries[total_entries].d_type = DT_REG;
        proc_entries[total_entries++].ino = 12;

        proc_entries[total_entries].name = "sys";
        proc_entries[total_entries].d_type = DT_DIR;
        proc_entries[total_entries++].ino = 13;

        off_t cur_idx = lseek(fd, 0, SEEK_CUR);
        if (cur_idx < 0) cur_idx = 0;
        if (cur_idx >= total_entries) return 0;

        size_t bytes_written = 0;
        char *out = (char *)dirp;

        while (cur_idx < total_entries) {
            size_t namelen = strlen(proc_entries[cur_idx].name);
            size_t reclen = (offsetof(struct linux_dirent64, d_name) + namelen + 1 + 7) & ~7ULL;
            if (bytes_written + reclen > count) {
                break;
            }

            struct linux_dirent64 *ld = (struct linux_dirent64 *)(out + bytes_written);
            ld->d_ino = proc_entries[cur_idx].ino;
            ld->d_off = cur_idx + 1;
            ld->d_reclen = (uint16_t)reclen;
            ld->d_type = proc_entries[cur_idx].d_type;
            memcpy(ld->d_name, proc_entries[cur_idx].name, namelen + 1);

            bytes_written += reclen;
            cur_idx++;
        }

        if (bytes_written == 0 && cur_idx < total_entries) {
            return -LINUX_EINVAL;
        }

        lseek(fd, cur_idx, SEEK_SET);
        return bytes_written;
    }

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
    if (nfds > 0 && !fds) return -LINUX_EFAULT;
    if (tmo_p && (tmo_p->tv_nsec < 0 || tmo_p->tv_nsec >= 1000000000L)) return -LINUX_EINVAL;
    int timeout_ms = -1;
    if (tmo_p) {
        timeout_ms = (int)(tmo_p->tv_sec * 1000 + tmo_p->tv_nsec / 1000000);
    }
    int ret = poll(fds, nfds, timeout_ms);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_pselect6(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                        const struct linux_timespec *timeout, const void *sig) {
    (void)sig;
    if (nfds < 0 || nfds > 1024) return -LINUX_EINVAL;
    if (timeout && (timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L)) return -LINUX_EINVAL;
    struct timespec ts;
    struct timespec *pts = NULL;
    if (timeout) {
        ts.tv_sec = timeout->tv_sec;
        ts.tv_nsec = timeout->tv_nsec;
        pts = &ts;
    }


    fd_set rfds, wfds, efds;
    if (readfds) memcpy(&rfds, readfds, sizeof(rfds));
    if (writefds) memcpy(&wfds, writefds, sizeof(wfds));
    if (exceptfds) memcpy(&efds, exceptfds, sizeof(efds));

    int ret = pselect(nfds,
                      readfds ? &rfds : NULL,
                      writefds ? &wfds : NULL,
                      exceptfds ? &efds : NULL,
                      pts,
                      NULL);

    if (ret < 0) return darwin_to_linux_errno(errno);

    if (readfds) memcpy(readfds, &rfds, sizeof(rfds));
    if (writefds) memcpy(writefds, &wfds, sizeof(wfds));
    if (exceptfds) memcpy(exceptfds, &efds, sizeof(efds));

    return ret;
}

#define LINUX_NSIG 65

/* Linux ARM64 to Darwin signal translation */
static inline int linux_to_darwin_signal(int lsig) {
    switch (lsig) {
        case 1:  return SIGHUP;     /* 1 */
        case 2:  return SIGINT;     /* 2 */
        case 3:  return SIGQUIT;    /* 3 */
        case 4:  return SIGILL;     /* 4 */
        case 5:  return SIGTRAP;    /* 5 */
        case 6:  return SIGABRT;    /* 6 */
        case 7:  return SIGBUS;     /* Darwin 10 */
        case 8:  return SIGFPE;     /* 8 */
        case 9:  return SIGKILL;    /* 9 */
        case 10: return SIGUSR1;    /* Darwin 30 */
        case 11: return SIGSEGV;    /* 11 */
        case 12: return SIGUSR2;    /* Darwin 31 */
        case 13: return SIGPIPE;    /* 13 */
        case 14: return SIGALRM;    /* 14 */
        case 15: return SIGTERM;    /* 15 */
        case 16: return SIGURG;     /* LINUX_SIGSTKFLT -> Darwin SIGURG (16) */
        case 17: return SIGCHLD;    /* Darwin 20 */
        case 18: return SIGCONT;    /* Darwin 19 */
        case 19: return SIGSTOP;    /* Darwin 17 */
        case 20: return SIGTSTP;    /* Darwin 18 */
        case 21: return SIGTTIN;    /* 21 */
        case 22: return SIGTTOU;    /* 22 */
        case 23: return SIGURG;     /* Darwin 16 */
        case 24: return SIGXCPU;    /* 24 */
        case 25: return SIGXFSZ;    /* 25 */
        case 26: return SIGVTALRM;  /* 26 */
        case 27: return SIGPROF;    /* 27 */
        case 28: return SIGWINCH;   /* 28 */
        case 29: return SIGIO;      /* Darwin 23 */
        case 30: return SIGINFO;    /* LINUX_SIGPWR -> Darwin SIGINFO (29) */
        case 31: return SIGSYS;     /* Darwin 12 */
        default: return (lsig > 0 && lsig < 32) ? lsig : 0;
    }
}

/* Darwin to Linux ARM64 signal translation */
static inline int darwin_to_linux_signal(int dsig) {
    switch (dsig) {
        case 1:  return 1;   /* LINUX_SIGHUP */
        case 2:  return 2;   /* LINUX_SIGINT */
        case 3:  return 3;   /* LINUX_SIGQUIT */
        case 4:  return 4;   /* LINUX_SIGILL */
        case 5:  return 5;   /* LINUX_SIGTRAP */
        case 6:  return 6;   /* LINUX_SIGABRT */
        case 7:  return 7;   /* SIGEMT -> LINUX_SIGBUS */
        case 8:  return 8;   /* LINUX_SIGFPE */
        case 9:  return 9;   /* LINUX_SIGKILL */
        case 10: return 7;   /* Darwin SIGBUS (10) -> LINUX_SIGBUS (7) */
        case 11: return 11;  /* LINUX_SIGSEGV */
        case 12: return 31;  /* Darwin SIGSYS (12) -> LINUX_SIGSYS (31) */
        case 13: return 13;  /* LINUX_SIGPIPE */
        case 14: return 14;  /* LINUX_SIGALRM */
        case 15: return 15;  /* LINUX_SIGTERM */
        case 16: return 23;  /* Darwin SIGURG (16) -> LINUX_SIGURG (23) */
        case 17: return 19;  /* Darwin SIGSTOP (17) -> LINUX_SIGSTOP (19) */
        case 18: return 20;  /* Darwin SIGTSTP (18) -> LINUX_SIGTSTP (20) */
        case 19: return 18;  /* Darwin SIGCONT (19) -> LINUX_SIGCONT (18) */
        case 20: return 17;  /* Darwin SIGCHLD (20) -> LINUX_SIGCHLD (17) */
        case 21: return 21;  /* LINUX_SIGTTIN */
        case 22: return 22;  /* LINUX_SIGTTOU */
        case 23: return 29;  /* Darwin SIGIO (23) -> LINUX_SIGIO (29) */
        case 24: return 24;  /* LINUX_SIGXCPU */
        case 25: return 25;  /* LINUX_SIGXFSZ */
        case 26: return 26;  /* LINUX_SIGVTALRM */
        case 27: return 27;  /* LINUX_SIGPROF */
        case 28: return 28;  /* LINUX_SIGWINCH */
        case 29: return 29;  /* Darwin SIGINFO (29) -> LINUX_SIGIO (29) */
        case 30: return 10;  /* Darwin SIGUSR1 (30) -> LINUX_SIGUSR1 (10) */
        case 31: return 12;  /* Darwin SIGUSR2 (31) -> LINUX_SIGUSR2 (12) */
        default: return (dsig > 0 && dsig < LINUX_NSIG) ? dsig : 0;
    }
}

int64_t handle_kill(pid_t pid, int sig) {
    if (sig < 0 || sig >= LINUX_NSIG) return -LINUX_EINVAL;
    if (pid == 1 && sig == 0) return 0;
    if (signalfd_deliver_signal(sig, NULL)) {
        return 0;
    }
    int d_sig = (sig == 0) ? 0 : linux_to_darwin_signal(sig);
    int ret = kill(pid, d_sig);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_tkill(int tid, int sig) {
    if (sig < 0 || sig >= LINUX_NSIG) return -LINUX_EINVAL;
    if (tid <= 0) return -LINUX_ESRCH;

    int d_sig = (sig == 0) ? 0 : linux_to_darwin_signal(sig);

    if (tid == getpid()) {
        if (sig == 0) return 0;
        if (signalfd_deliver_signal(sig, NULL)) {
            return 0;
        }
        int ret = kill(getpid(), d_sig);
        if (ret < 0) return darwin_to_linux_errno(errno);
        return 0;
    }

    pthread_t target_th = NULL;
    bool found = false;

    pthread_mutex_lock(&g_thread_table_mutex);
    for (int i = 0; i < LTM_MAX_THREADS; i++) {
        if (g_thread_table[i].active && g_thread_table[i].tid == tid) {
            target_th = g_thread_table[i].pthread;
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_thread_table_mutex);

    if (!found) {
        int r = kill(tid, 0);
        if (r < 0) return darwin_to_linux_errno(errno);
        if (sig == 0) return 0;
        r = kill(tid, d_sig);
        if (r < 0) return darwin_to_linux_errno(errno);
        return 0;
    }

    if (sig == 0) return 0;
    if (signalfd_deliver_signal(sig, NULL)) {
        return 0;
    }
    int prc = pthread_kill(target_th, d_sig);
    if (prc != 0) return darwin_to_linux_errno(prc);
    return 0;
}

int64_t handle_tgkill(int tgid, int tid, int sig) {
    if (sig < 0 || sig >= LINUX_NSIG) return -LINUX_EINVAL;
    if (tgid <= 0 || tid <= 0) return -LINUX_ESRCH;
    if (tgid != getpid()) {
        int r = kill(tgid, 0);
        if (r < 0) return darwin_to_linux_errno(errno);
    }
    return handle_tkill(tid, sig);
}

int64_t handle_sigaltstack(const void *ss, void *oss) {
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
    if (ss) {
        const struct {
            void *ss_sp;
            int ss_flags;
            int __pad;
            size_t ss_size;
        } *in = (const void *)ss;
        if (in->ss_flags & ~2 /* SS_DISABLE */) {
            return -LINUX_EINVAL;
        }
        if (!(in->ss_flags & 2) && in->ss_size < 2048) {
            return -LINUX_ENOMEM;
        }
    }
    return 0;
}

static struct linux_sigaction g_guest_sigactions[LINUX_NSIG];

struct ltm_sigframe {
    struct linux_siginfo  siginfo;
    struct linux_ucontext uc;
    arm_thread_state64_t  saved_state;
    arm_neon_state64_t    saved_neon;
    uint64_t              magic;
};

#define LTM_SIGFRAME_MAGIC 0x4c544d5f534947ULL /* "LTM_SIG" */

static __thread int s_in_host_syscall = 0;
static __thread sig_atomic_t s_pending_signal = 0;
static __thread siginfo_t s_pending_siginfo;

void ltm_set_in_host_syscall(int in_sys) {
    s_in_host_syscall = in_sys;
}

void ltm_check_and_deliver_pending_signals(void *state_ptr) {
    if (!s_pending_signal) return;
    int l_sig = (int)s_pending_signal;
    s_pending_signal = 0;

    if (l_sig <= 0 || l_sig >= LINUX_NSIG) return;
    struct linux_sigaction *act = &g_guest_sigactions[l_sig];
    if (!act->l_handler || act->l_handler == (void (*)(int))SIG_DFL || act->l_handler == (void (*)(int))SIG_IGN) {
        return;
    }

    arm_thread_state64_t *state = (arm_thread_state64_t *)state_ptr;
    uintptr_t sp = state->__sp;
    sp -= sizeof(struct ltm_sigframe);
    sp &= ~0xfULL;

    struct ltm_sigframe *frame = (struct ltm_sigframe *)sp;
    memset(&frame->siginfo, 0, sizeof(frame->siginfo));
    frame->siginfo.si_signo = l_sig;
    frame->siginfo.si_code = s_pending_siginfo.si_code;
    if (l_sig == 17 /* LINUX_SIGCHLD */) {
        frame->siginfo._sifields._sigchld._pid = s_pending_siginfo.si_pid;
        frame->siginfo._sifields._sigchld._status = s_pending_siginfo.si_status;
    }
    if (l_sig == 4 || l_sig == 7 || l_sig == 11) {
        frame->siginfo._sifields._sigfault._addr = s_pending_siginfo.si_addr;
    }

    memcpy(&frame->saved_state, state, sizeof(arm_thread_state64_t));
    frame->magic = LTM_SIGFRAME_MAGIC;

    memset(&frame->uc, 0, sizeof(frame->uc));
    frame->uc.uc_stack.ss_sp = (void *)state->__sp;
    for (int i = 0; i < 31; i++) {
        frame->uc.uc_mcontext.regs[i] = state->__x[i];
    }
    frame->uc.uc_mcontext.sp = state->__sp;
    frame->uc.uc_mcontext.pc = state->__pc;
    frame->uc.uc_mcontext.pstate = state->__cpsr;

    state->__x[0] = l_sig;
    state->__x[1] = (uintptr_t)&frame->siginfo;
    state->__x[2] = (uintptr_t)&frame->uc;
    state->__sp = sp;
    uintptr_t restorer = ((act->l_flags & 0x04000000ULL) && act->l_restorer)
                         ? (uintptr_t)act->l_restorer
                         : (uintptr_t)ltm_sigreturn_stub;
    state->__lr = restorer;
    state->__pc = (uintptr_t)act->l_handler;
    state->__x[16] = (uintptr_t)act->l_handler;
}

static void ltm_host_signal_handler(int d_sig, siginfo_t *info, void *ucontext) {
    int l_sig = darwin_to_linux_signal(d_sig);
    if (l_sig <= 0 || l_sig >= LINUX_NSIG) return;

    struct linux_sigaction *act = &g_guest_sigactions[l_sig];
    if (!act->l_handler || act->l_handler == (void (*)(int))SIG_DFL || act->l_handler == (void (*)(int))SIG_IGN) {
        return;
    }

    /* If the signal arrived while the host is executing a blocking syscall, defer delivery to avoid clobbering host C frame */
    if (s_in_host_syscall) {
        s_pending_signal = l_sig;
        if (info) s_pending_siginfo = *info;
        return;
    }

    ucontext_t *uc = (ucontext_t *)ucontext;
    arm_thread_state64_t *cur_state = &uc->uc_mcontext->__ss;

    /* Recursion protection for fatal exception signals */
    if (l_sig == 4 || l_sig == 7 || l_sig == 8 || l_sig == 11 || l_sig == 31) {
        if (s_crash_depth > 0) {
            ltm_log_error("Recursive crash signal %d delivered (depth=%d): PC=0x%llx. Terminating cleanly.",
                          l_sig, s_crash_depth, (unsigned long long)cur_state->__pc);
            _exit(128 + l_sig);
        }
        s_crash_depth++;
    }

    /* Validate stack pointer before attempting to write guest sigframe */
    if (cur_state->__sp < 0x1000 || (cur_state->__sp & 0x7) != 0) {
        ltm_log_error("Guest crash signal %d with invalid SP=0x%llx. Terminating cleanly.",
                      l_sig, (unsigned long long)cur_state->__sp);
        _exit(128 + l_sig);
    }

    /* Push saved state and siginfo onto guest stack */
    uintptr_t sp = cur_state->__sp;
    sp -= sizeof(struct ltm_sigframe);
    sp &= ~0xfULL;

    struct ltm_sigframe *frame = (struct ltm_sigframe *)sp;
    memset(&frame->siginfo, 0, sizeof(frame->siginfo));
    frame->siginfo.si_signo = l_sig;
    frame->siginfo.si_code = info ? info->si_code : 0;
    if (info && l_sig == 17 /* LINUX_SIGCHLD */) {
        frame->siginfo._sifields._sigchld._pid = info->si_pid;
        frame->siginfo._sifields._sigchld._status = info->si_status;
    }
    if (info && (l_sig == 4 || l_sig == 7 || l_sig == 11)) {
        frame->siginfo._sifields._sigfault._addr = info->si_addr;
    }

    memcpy(&frame->saved_state, cur_state, sizeof(arm_thread_state64_t));
    memcpy(&frame->saved_neon, &uc->uc_mcontext->__ns, sizeof(arm_neon_state64_t));
    frame->magic = LTM_SIGFRAME_MAGIC;

    /* Populate standard Linux ARM64 ucontext */
    memset(&frame->uc, 0, sizeof(frame->uc));
    frame->uc.uc_stack.ss_sp = (void *)cur_state->__sp;
    for (int i = 0; i < 31; i++) {
        frame->uc.uc_mcontext.regs[i] = cur_state->__x[i];
    }
    frame->uc.uc_mcontext.sp = cur_state->__sp;
    frame->uc.uc_mcontext.pc = cur_state->__pc;
    frame->uc.uc_mcontext.pstate = cur_state->__cpsr;
    if (info && (l_sig == 4 || l_sig == 7 || l_sig == 11)) {
        frame->uc.uc_mcontext.fault_address = (uint64_t)info->si_addr;
    }

    /* Only log crash dumps for fatal exception signals */
    if (l_sig == 4 /* SIGILL */ || l_sig == 7 /* SIGBUS */ ||
        l_sig == 8 /* SIGFPE */ || l_sig == 11 /* SIGSEGV */ || l_sig == 31 /* SIGSYS */) {
        uint32_t fault_insn = 0;
        if (cur_state->__pc >= 0x1000) {
            fault_insn = *(uint32_t *)cur_state->__pc;
        }
        ltm_log_error("Guest crash signal %d delivered: PC=0x%llx (insn=0x%08x), fault_addr=%p",
                      l_sig, (unsigned long long)cur_state->__pc, fault_insn, info ? info->si_addr : NULL);
        for (int i = 0; i < 31; i += 2) {
            ltm_log_error("  x%-2d = 0x%016llx  x%-2d = 0x%016llx",
                          i, (unsigned long long)cur_state->__x[i],
                          i+1, (i+1 < 31) ? (unsigned long long)cur_state->__x[i+1] : 0ULL);
        }
        ltm_log_error("  sp   = 0x%016llx  lr   = 0x%016llx",
                      (unsigned long long)cur_state->__sp, (unsigned long long)cur_state->__lr);
        if (cur_state->__sp >= 0x1000) {
            uint64_t *stk = (uint64_t *)cur_state->__sp;
            ltm_log_error("Stack dump:");
            for (int i = 0; i < 32; i += 2) {
                ltm_log_error("  sp+%03x: 0x%016llx 0x%016llx",
                              i * 8, (unsigned long long)stk[i], (unsigned long long)stk[i+1]);
            }
        }
        if (cur_state->__fp >= 0x1000) {
            uint64_t *fp = (uint64_t *)cur_state->__fp;
            ltm_log_error("=== CALLSTACK BACKTRACE ===");
            int depth = 0;
            while (fp >= (uint64_t *)0x1000 && depth < 20) {
                ltm_log_error("  #%d: fp=%p, lr=0x%llx", depth, fp, (unsigned long long)fp[1]);
                fp = (uint64_t *)fp[0];
                depth++;
            }
        }
    }

    /* Setup registers to execute guest signal handler preserving callee-saved context */
    arm_thread_state64_t target_state = *cur_state;
    target_state.__x[0] = l_sig;                          /* signo */
    target_state.__x[1] = (uintptr_t)&frame->siginfo;     /* siginfo_t * */
    target_state.__x[2] = (uintptr_t)&frame->uc;          /* ucontext_t * */
    target_state.__sp = sp;
    uintptr_t restorer = ((act->l_flags & 0x04000000ULL) && act->l_restorer)
                         ? (uintptr_t)act->l_restorer
                         : (uintptr_t)ltm_sigreturn_stub;
    target_state.__lr = restorer;
    target_state.__pc = (uintptr_t)act->l_handler;
    target_state.__x[16] = (uintptr_t)act->l_handler;

    /* Unmask the signal only if SA_NODEFER was explicitly requested */
    if (act->l_flags & 0x40000000ULL /* LINUX_SA_NODEFER */) {
        sigset_t set;
        sigemptyset(&set);
        pthread_sigmask(SIG_SETMASK, &set, NULL);
    }

    /* Resume execution into guest handler bypassing Darwin signal trampoline */
    ltm_resume_context(&target_state);
}

int64_t handle_rt_sigaction(int signum, const struct linux_sigaction *act, struct linux_sigaction *oldact, size_t sigsetsize) {
    if (sigsetsize != 8) return -LINUX_EINVAL;
    if (signum <= 0 || signum >= LINUX_NSIG) return -LINUX_EINVAL;
    if (signum == 9 /* LINUX_SIGKILL */ || signum == 19 /* LINUX_SIGSTOP */) return -LINUX_EINVAL;
    if (signum == 5 /* LINUX_SIGTRAP */) return 0; /* Keep SIGTRAP for LTM dispatcher */

    if (oldact) {
        *oldact = g_guest_sigactions[signum];
    }

    if (act) {
        g_guest_sigactions[signum] = *act;

        int d_sig = linux_to_darwin_signal(signum);
        if (d_sig > 0 && d_sig != SIGTRAP && d_sig != SIGKILL && d_sig != SIGSTOP) {
            struct sigaction d_sa;
            memset(&d_sa, 0, sizeof(d_sa));

            if (act->l_handler == (void (*)(int))SIG_DFL) {
                d_sa.sa_handler = SIG_DFL;
                sigaction(d_sig, &d_sa, NULL);
            } else if (act->l_handler == (void (*)(int))SIG_IGN) {
                d_sa.sa_handler = SIG_IGN;
                sigaction(d_sig, &d_sa, NULL);
            } else {
                d_sa.sa_sigaction = ltm_host_signal_handler;
                d_sa.sa_flags = SA_SIGINFO;
                if (act->l_flags & 0x40000000ULL /* LINUX_SA_NODEFER */) {
                    d_sa.sa_flags |= SA_NODEFER;
                }
                if (act->l_flags & 0x08000000ULL /* LINUX_SA_ONSTACK */) {
                    d_sa.sa_flags |= SA_ONSTACK;
                }
                sigemptyset(&d_sa.sa_mask);
                sigaction(d_sig, &d_sa, NULL);
            }
        }
    }

    return 0;
}

int64_t handle_rt_sigreturn(void *state_ptr) {
    arm_thread_state64_t *cur_state = (arm_thread_state64_t *)state_ptr;
    uintptr_t sp = cur_state->__sp;
    if (sp < 0x1000 || (sp & 0x7) != 0) return -LINUX_EINVAL;

    mach_vm_address_t vaddr = (mach_vm_address_t)sp;
    mach_vm_size_t vsize = 0;
    vm_region_basic_info_data_64_t vinfo;
    mach_msg_type_number_t vcount = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t obj = MACH_PORT_NULL;
    kern_return_t kr = mach_vm_region(mach_task_self(), &vaddr, &vsize,
                                      VM_REGION_BASIC_INFO_64,
                                      (vm_region_info_t)&vinfo, &vcount, &obj);
    if (kr != KERN_SUCCESS || (mach_vm_address_t)sp < vaddr ||
        (mach_vm_address_t)sp + sizeof(struct ltm_sigframe) > vaddr + vsize ||
        !(vinfo.protection & VM_PROT_READ)) {
        return -LINUX_EINVAL;
    }

    struct ltm_sigframe *frame = (struct ltm_sigframe *)sp;
    if (frame->magic != LTM_SIGFRAME_MAGIC) {
        return -LINUX_EINVAL;
    }

    /* Ensure signals are not masked when returning to main execution */
    sigset_t set;
    sigemptyset(&set);
    pthread_sigmask(SIG_SETMASK, &set, NULL);

    /* Restore all integer registers, condition flags, SP, and PC via ltm_resume_context */
    ltm_resume_context(&frame->saved_state);
    return 0;
}

int64_t handle_rt_sigprocmask(int how, const uint64_t *set, uint64_t *oldset, size_t sigsetsize) {
    if (sigsetsize != 8) return -LINUX_EINVAL;
    if (oldset) {
        sigset_t cur;
        sigemptyset(&cur);
        pthread_sigmask(SIG_SETMASK, NULL, &cur);
        uint64_t l_old = 0;
        for (int i = 1; i < 32; i++) {
            if (sigismember(&cur, i)) {
                int l_sig = darwin_to_linux_signal(i);
                if (l_sig > 0 && l_sig < 64) {
                    l_old |= (1ULL << (l_sig - 1));
                }
            }
        }
        *oldset = l_old;
    }
    if (set) {
        if (how != 0 /* SIG_BLOCK */ && how != 1 /* SIG_UNBLOCK */ && how != 2 /* SIG_SETMASK */) {
            return -LINUX_EINVAL;
        }
        sigset_t d_set;
        sigemptyset(&d_set);
        for (int i = 1; i < 64; i++) {
            if (*set & (1ULL << (i - 1))) {
                int d_sig = linux_to_darwin_signal(i);
                if (d_sig > 0 && d_sig != SIGKILL && d_sig != SIGSTOP) {
                    sigaddset(&d_set, d_sig);
                }
            }
        }
        int d_how = (how == 0) ? SIG_BLOCK : (how == 1 ? SIG_UNBLOCK : SIG_SETMASK);
        pthread_sigmask(d_how, &d_set, NULL);
    }
    return 0;
}


/* Memory Management */
static pthread_mutex_t g_brk_mutex = PTHREAD_MUTEX_INITIALIZER;

int64_t handle_brk(uintptr_t brk_addr) {
    pthread_mutex_lock(&g_brk_mutex);
    if (brk_addr == 0 || brk_addr < g_ltm.brk_start) {
        int64_t ret = (int64_t)g_ltm.brk_current;
        pthread_mutex_unlock(&g_brk_mutex);
        return ret;
    }

    if (brk_addr > g_ltm.brk_current) {
        uintptr_t page_cur = PAGE_ROUND_UP(g_ltm.brk_current);
        uintptr_t page_new = PAGE_ROUND_UP(brk_addr);

        if (page_new > page_cur) {
            mach_vm_address_t check_addr = (mach_vm_address_t)page_cur;
            mach_vm_size_t check_size = 0;
            vm_region_basic_info_data_64_t info;
            mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t object_name;
            kern_return_t kr = mach_vm_region(mach_task_self(), &check_addr, &check_size,
                                              VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object_name);
            if (kr == KERN_SUCCESS && check_addr < (mach_vm_address_t)page_new) {
                /* Collision detected with an existing mapping; reject expansion per Linux ABI */
                int64_t ret = (int64_t)g_ltm.brk_current;
                pthread_mutex_unlock(&g_brk_mutex);
                return ret;
            }

            void *p = mmap((void *)page_cur, page_new - page_cur,
                           PROT_READ | PROT_WRITE,
                           MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
            if (p == MAP_FAILED) {
                ltm_log_error("brk mmap failed for expansion to %p", (void *)page_new);
                int64_t ret = (int64_t)g_ltm.brk_current;
                pthread_mutex_unlock(&g_brk_mutex);
                return ret;
            }
        }
        g_ltm.brk_current = brk_addr;
        int64_t ret = (int64_t)g_ltm.brk_current;
        pthread_mutex_unlock(&g_brk_mutex);
        return ret;
    }

    if (brk_addr < g_ltm.brk_current) {
        uintptr_t page_cur = PAGE_ROUND_UP(g_ltm.brk_current);
        uintptr_t page_new = PAGE_ROUND_UP(brk_addr);

        if (page_cur > page_new) {
            munmap((void *)page_new, page_cur - page_new);
        }
        g_ltm.brk_current = brk_addr;
        int64_t ret = (int64_t)g_ltm.brk_current;
        pthread_mutex_unlock(&g_brk_mutex);
        return ret;
    }

    int64_t ret = (int64_t)g_ltm.brk_current;
    pthread_mutex_unlock(&g_brk_mutex);
    return ret;
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
    return 0;
}

int64_t handle_geteuid(void) {
    return 0;
}

int64_t handle_getgid(void) {
    return 0;
}

int64_t handle_getegid(void) {
    return 0;
}

int64_t handle_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid) {
    if (!ruid || !euid || !suid) return -LINUX_EFAULT;
    *ruid = 0;
    *euid = 0;
    *suid = 0;
    return 0;
}

int64_t handle_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid) {
    if (!rgid || !egid || !sgid) return -LINUX_EFAULT;
    *rgid = 0;
    *egid = 0;
    *sgid = 0;
    return 0;
}

int64_t handle_getgroups(int size, gid_t list[]) {
    if (size < 0) return -LINUX_EINVAL;
    if (size == 0) return 1; /* Linux root group count */
    if (!list) return -LINUX_EFAULT;
    list[0] = 0;
    return 1;
}

int64_t handle_setgroups(size_t size, const gid_t *list) {
    if ((long)size < 0 || size > 65536) return -LINUX_EINVAL;
    if (size > 0 && !list) return -LINUX_EFAULT;
    return 0;
}


int64_t handle_capget(void *hdr, void *data) {
    if (hdr) {
        uint32_t *h = (uint32_t *)hdr;
        h[0] = 0x20080522; /* _LINUX_CAPABILITY_VERSION_3 */
    }
    if (data) {
        uint32_t *d = (uint32_t *)data;
        d[0] = 0xffffffff;
        d[1] = 0xffffffff;
        d[2] = 0xffffffff;
        d[3] = 0xffffffff;
        d[4] = 0xffffffff;
        d[5] = 0xffffffff;
    }
    return 0;
}

int64_t handle_capset(void *hdr, const void *data) {
    (void)hdr;
    (void)data;
    return 0;
}

int64_t handle_gettid(void) {
    return ltm_get_thread_tid();
}

static char g_hostname[65] = "ltm-mac";
static char g_domainname[65] = "(none)";

int64_t handle_uname(struct linux_utsname *buf) {
    if (!buf) return -LINUX_EFAULT;
    memset(buf, 0, sizeof(*buf));

    strncpy(buf->sysname, "Linux", sizeof(buf->sysname) - 1);
    strncpy(buf->nodename, g_hostname, sizeof(buf->nodename) - 1);
    strncpy(buf->release, LTM_KERNEL_VERSION_NAME, sizeof(buf->release) - 1);
    strncpy(buf->version, "#1 SMP PREEMPT_DYNAMIC Wed Oct 14 00:00:00 UTC 2026", sizeof(buf->version) - 1);
    strncpy(buf->machine, "aarch64", sizeof(buf->machine) - 1);
    strncpy(buf->domainname, g_domainname, sizeof(buf->domainname) - 1);

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
    if (clock_id < 0 || clock_id > 15) return -LINUX_EINVAL;

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
    ltm_trace("sys_exit(%d) on thread %lld", status, (long long)ltm_get_thread_tid());

    /* Clean up robust futex lists for terminating thread */
    ltm_exit_robust_list();

    int *tid_ptr = ltm_get_child_tid_ptr();
    if (tid_ptr) {
        mach_vm_address_t vaddr = (mach_vm_address_t)tid_ptr;
        if (mach_vm_protect(mach_task_self(), vaddr, sizeof(int), FALSE, VM_PROT_READ | VM_PROT_WRITE) == KERN_SUCCESS) {
            *tid_ptr = 0;
            handle_futex((uint32_t *)tid_ptr, LINUX_FUTEX_WAKE, 1, NULL, NULL, 0);
        }
    }

    int my_tid = (int)ltm_get_thread_tid();
    pthread_mutex_lock(&g_thread_table_mutex);
    for (int i = 0; i < LTM_MAX_THREADS; i++) {
        if (g_thread_table[i].tid == my_tid) {
            g_thread_table[i].active = 0;
            break;
        }
    }
    pthread_mutex_unlock(&g_thread_table_mutex);

    int remaining = atomic_fetch_sub(&g_active_thread_count, 1) - 1;
    if (remaining <= 0) {
        exit(status);
    } else if (s_has_exit_jmp) {
        siglongjmp(s_thread_exit_jmp, 1);
    } else {
        pthread_exit(NULL);
    }
}

int64_t handle_exit_group(int status) {
    ltm_trace("sys_exit_group(%d)", status);
    exit(status);
}

/* Threading & Futex */
int64_t handle_set_tid_address(int *tidptr) {
    ltm_set_child_tid_ptr(tidptr);
    return ltm_get_thread_tid();
}

int64_t handle_futex(uint32_t *uaddr, int op, uint32_t val,
                     const struct linux_timespec *timeout, uint32_t *uaddr2, uint32_t val3) {
    (void)uaddr2;
    if (!uaddr) return -LINUX_EFAULT;
    if (((uintptr_t)uaddr & 3) != 0) return -LINUX_EINVAL;

    int cmd = op & ~(LINUX_FUTEX_PRIVATE_FLAG | LINUX_FUTEX_CLOCK_REALTIME);
    futex_bucket_t *b = get_futex_bucket(uaddr);

    if (cmd == LINUX_FUTEX_WAIT || cmd == LINUX_FUTEX_WAIT_BITSET) {
        if (cmd == LINUX_FUTEX_WAIT_BITSET && val3 == 0) {
            return -LINUX_EINVAL;
        }

        pthread_mutex_lock(&b->mutex);
        if (*(volatile uint32_t *)uaddr != val) {
            pthread_mutex_unlock(&b->mutex);
            return -LINUX_EAGAIN;
        }

        int r = 0;
        if (timeout) {
            struct timespec abstime;
            if (op & LINUX_FUTEX_CLOCK_REALTIME) {
                abstime.tv_sec = timeout->tv_sec;
                abstime.tv_nsec = timeout->tv_nsec;
            } else if (cmd == LINUX_FUTEX_WAIT_BITSET) {
                /* Absolute CLOCK_MONOTONIC timeout: convert to CLOCK_REALTIME for Darwin */
                struct timespec now_mono, now_real;
                clock_gettime(CLOCK_MONOTONIC, &now_mono);
                clock_gettime(CLOCK_REALTIME, &now_real);
                int64_t diff_sec = timeout->tv_sec - now_mono.tv_sec;
                int64_t diff_nsec = timeout->tv_nsec - now_mono.tv_nsec;
                abstime.tv_sec = now_real.tv_sec + diff_sec;
                abstime.tv_nsec = now_real.tv_nsec + diff_nsec;
                if (abstime.tv_nsec >= 1000000000L) {
                    abstime.tv_sec += abstime.tv_nsec / 1000000000L;
                    abstime.tv_nsec %= 1000000000L;
                } else if (abstime.tv_nsec < 0) {
                    int64_t adj = ((-abstime.tv_nsec) + 999999999L) / 1000000000L;
                    abstime.tv_sec -= adj;
                    abstime.tv_nsec += adj * 1000000000L;
                }
            } else {
                struct timespec now;
                clock_gettime(CLOCK_REALTIME, &now);
                abstime.tv_sec = now.tv_sec + timeout->tv_sec;
                abstime.tv_nsec = now.tv_nsec + timeout->tv_nsec;
                if (abstime.tv_nsec >= 1000000000L) {
                    abstime.tv_sec += 1;
                    abstime.tv_nsec -= 1000000000L;
                }
            }
            r = pthread_cond_timedwait(&b->cond, &b->mutex, &abstime);
        } else {
            r = pthread_cond_wait(&b->cond, &b->mutex);
        }
        pthread_mutex_unlock(&b->mutex);

        if (r == ETIMEDOUT) return -LINUX_ETIMEDOUT;
        return 0;
    }

    if (cmd == LINUX_FUTEX_WAKE || cmd == LINUX_FUTEX_WAKE_BITSET) {
        if (cmd == LINUX_FUTEX_WAKE_BITSET && val3 == 0) {
            return -LINUX_EINVAL;
        }
        pthread_mutex_lock(&b->mutex);
        pthread_cond_broadcast(&b->cond);
        pthread_mutex_unlock(&b->mutex);
        return val > 0 ? (int64_t)val : 1;
    }

    return -LINUX_ENOSYS;
}

struct ltm_thread_arg {
    arm_thread_state64_t state;
    uint64_t flags;
    void *stack;
    int *ptid;
    void *tls;
    int *ctid;
    int tid;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int started;
};

static void *ltm_thread_entry(void *arg) {
    struct ltm_thread_arg *targ = (struct ltm_thread_arg *)arg;

    /* Allocate dedicated alternate signal stack for this thread (1MB) */
    void *alt_stack = mmap(NULL, 1024 * 1024, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (alt_stack != MAP_FAILED) {
        stack_t ss = {
            .ss_sp = alt_stack,
            .ss_size = 1024 * 1024,
            .ss_flags = 0
        };
        sigaltstack(&ss, NULL);
    }

    pthread_mutex_lock(&targ->lock);
    struct ltm_thread_arg local = *targ;

    /* Initialize thread-local state */
    s_is_main_thread = 0;
    s_thread_tid = local.tid;
    if ((local.flags & LINUX_CLONE_SETTLS) && local.tls) {
        ltm_set_guest_tls((uintptr_t)local.tls);
    }
    if ((local.flags & LINUX_CLONE_CHILD_SETTID) && local.ctid) {
        *local.ctid = local.tid;
    }
    if (local.flags & LINUX_CLONE_CHILD_CLEARTID) {
        ltm_set_child_tid_ptr(local.ctid);
    }

    targ->started = 1;
    pthread_cond_signal(&targ->cond);
    pthread_mutex_unlock(&targ->lock);

    /* Ensure SIGTRAP is unblocked for LTM syscall and instruction trapping */
    sigset_t unblock_mask;
    sigemptyset(&unblock_mask);
    sigaddset(&unblock_mask, SIGTRAP);
    pthread_sigmask(SIG_UNBLOCK, &unblock_mask, NULL);

    ltm_log_debug("Thread %d started (guest SP=%p, PC=0x%llx, TLS=0x%lx)",
                  local.tid, (void *)local.state.__sp,
                  (unsigned long long)local.state.__pc,
                  (unsigned long)ltm_get_guest_tls());

    s_has_exit_jmp = true;
    if (sigsetjmp(s_thread_exit_jmp, 1) != 0) {
        /* Thread has terminated via handle_exit! We are safely back on Darwin native pthread stack */
        stack_t ss = { .ss_flags = SS_DISABLE };
        sigaltstack(&ss, NULL);
        if (alt_stack != MAP_FAILED) {
            munmap(alt_stack, 1024 * 1024);
        }
        return NULL;
    }

    /* Resume guest execution on this native pthread */
    ltm_resume_context(&local.state);

    return NULL;
}

int64_t handle_clone(uint64_t flags, void *stack, int *ptid, void *tls, int *ctid, void *state_ptr) {
    arm_thread_state64_t *state = (arm_thread_state64_t *)state_ptr;

    if (flags & LINUX_CLONE_THREAD) {
        /* Thread Creation */
        if (!stack) return -LINUX_EINVAL;

        int child_tid = ltm_allocate_tid();

        if ((flags & LINUX_CLONE_PARENT_SETTID) && ptid) {
            *ptid = child_tid;
        }

        struct ltm_thread_arg targ;
        memset(&targ, 0, sizeof(targ));
        targ.flags = flags;
        targ.stack = (void *)((uintptr_t)stack & ~0x0fULL);
        targ.ptid = ptid;
        targ.tls = tls;
        targ.ctid = ctid;
        targ.tid = child_tid;
        targ.started = 0;
        if (state) {
            targ.state = *state;
            targ.state.__x[0] = 0;
            targ.state.__pc += 4;
            targ.state.__sp = (uintptr_t)targ.stack;
        }

        pthread_mutex_init(&targ.lock, NULL);
        pthread_cond_init(&targ.cond, NULL);

        /* Register in thread table */
        pthread_mutex_lock(&g_thread_table_mutex);
        int slot = -1;
        for (int i = 0; i < LTM_MAX_THREADS; i++) {
            if (!g_thread_table[i].active) {
                slot = i;
                g_thread_table[i].tid = child_tid;
                g_thread_table[i].active = 1;
                break;
            }
        }
        pthread_mutex_unlock(&g_thread_table_mutex);

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        atomic_fetch_add(&g_active_thread_count, 1);

        pthread_t th;
        int prc = pthread_create(&th, &attr, ltm_thread_entry, &targ);
        pthread_attr_destroy(&attr);

        if (prc != 0) {
            atomic_fetch_sub(&g_active_thread_count, 1);
            if (slot >= 0) {
                pthread_mutex_lock(&g_thread_table_mutex);
                g_thread_table[slot].active = 0;
                pthread_mutex_unlock(&g_thread_table_mutex);
            }
            pthread_mutex_destroy(&targ.lock);
            pthread_cond_destroy(&targ.cond);
            return darwin_to_linux_errno(prc);
        }

        if (slot >= 0) {
            pthread_mutex_lock(&g_thread_table_mutex);
            g_thread_table[slot].pthread = th;
            pthread_mutex_unlock(&g_thread_table_mutex);
        }

        /* Wait until child thread has initialized and copied its arguments */
        pthread_mutex_lock(&targ.lock);
        while (!targ.started) {
            pthread_cond_wait(&targ.cond, &targ.lock);
        }
        pthread_mutex_unlock(&targ.lock);
        pthread_mutex_destroy(&targ.lock);
        pthread_cond_destroy(&targ.cond);

        return child_tid;
    }

    /* Process Fork */
    pid_t pid = fork();
    if (pid < 0) return darwin_to_linux_errno(errno);

    if (pid == 0) {
        /* Child process: reset thread state and re-init dispatcher */
        s_is_main_thread = 1;
        s_thread_tid = 0;
        s_crash_depth = 0;
        s_has_exit_jmp = false;
        atomic_store(&g_active_thread_count, 1);
        atomic_store(&g_next_thread_tid, 0);
        memset(g_thread_table, 0, sizeof(g_thread_table));

        ltm_reset_on_fork();
        syscall_dispatcher_init();

        if (state) {
            state->__x[0] = 0;
            state->__pc += 4;
            if (stack) {
                state->__sp = (uintptr_t)stack & ~0x0fULL;
            }
        }
        if ((flags & LINUX_CLONE_SETTLS) && tls) {
            ltm_set_guest_tls((uintptr_t)tls);
        }
        if ((flags & LINUX_CLONE_CHILD_SETTID) && ctid) {
            *ctid = getpid();
        }
        if (flags & LINUX_CLONE_CHILD_CLEARTID) {
            ltm_set_child_tid_ptr(ctid);
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
    if (!pathname || !buf) return -LINUX_EFAULT;
    if (bufsiz == 0) return -LINUX_EINVAL;

    if (sandbox_is_virtual_path(pathname)) {
        return sandbox_virtual_readlink(pathname, buf, bufsiz);
    }

    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), AT_SYMLINK_NOFOLLOW);
    if (res < 0) return res;
    if (res == 1) {
        return sandbox_virtual_readlink(host_path, buf, bufsiz);
    }

    ssize_t ret = readlink(host_path, buf, bufsiz);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_prlimit64(int pid, int resource, const struct linux_rlimit64 *new_limit, struct linux_rlimit64 *old_limit) {
    if (resource < 0 || resource > 15) return -LINUX_EINVAL;
    if (pid < 0) return -LINUX_ESRCH;
    if (pid > 0 && pid != getpid()) {
        if (kill(pid, 0) < 0) return -LINUX_ESRCH;
    }
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
    if (flags & ~(1U | 2U | 4U)) return -LINUX_EINVAL;
    if (buflen == 0) return 0;
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
        if (WIFSIGNALED(dstatus)) {
            int d_sig = WTERMSIG(dstatus);
            int l_sig = darwin_to_linux_signal(d_sig);
            *status = (dstatus & ~0x7f) | (l_sig & 0x7f);
        } else if (WIFSTOPPED(dstatus)) {
            int d_sig = WSTOPSIG(dstatus);
            int l_sig = darwin_to_linux_signal(d_sig);
            *status = (dstatus & ~0xff00) | ((l_sig & 0xff) << 8);
        } else {
            *status = dstatus;
        }
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
    if (pid < 0 || pgid < 0) return -LINUX_EINVAL;
    if (pid > 0 && pid != getpid()) return -LINUX_ESRCH;
    return 0;
}

int64_t handle_getpgid(pid_t pid) {
    if (pid < 0) return -LINUX_EINVAL;
    if (pid > 0 && pid != getpid()) return -LINUX_ESRCH;
    pid_t ret = getpgrp();
    if (ret <= 0) ret = getpid();
    return ret;
}

int64_t handle_execve(const char *pathname, char *const argv[], char *const envp[]) {
    if (!pathname) return -LINUX_EFAULT;

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

    /* Check if target executable is a script with shebang (#!) */
    char shebang_interp[512] = {0};
    char shebang_arg[512] = {0};
    bool is_script = false;
    int sfd = open(host_path, O_RDONLY);
    if (sfd >= 0) {
        char magic[2];
        if (read(sfd, magic, 2) == 2 && magic[0] == '#' && magic[1] == '!') {
            char sline[1024];
            ssize_t sr = read(sfd, sline, sizeof(sline) - 1);
            if (sr > 0) {
                sline[sr] = '\0';
                char *nl = strchr(sline, '\n');
                if (nl) *nl = '\0';
                char *p = sline;
                while (*p == ' ' || *p == '\t') p++;
                char *interp = p;
                while (*p && *p != ' ' && *p != '\t') p++;
                if (*p) {
                    *p++ = '\0';
                    while (*p == ' ' || *p == '\t') p++;
                    strncpy(shebang_arg, p, sizeof(shebang_arg) - 1);
                }
                strncpy(shebang_interp, interp, sizeof(shebang_interp) - 1);
                if (shebang_interp[0] != '\0') {
                    is_script = true;
                }
            }
        }
        close(sfd);
    }

    int guest_argc = 0;
    while (argv && argv[guest_argc]) {
        guest_argc++;
    }

    int max_args = guest_argc + 16;
    char **new_argv = malloc(max_args * sizeof(char *));
    if (!new_argv) return -LINUX_ENOMEM;

    int n = 0;
    new_argv[n++] = ltm_path;
    new_argv[n++] = "-r";
    new_argv[n++] = g_ltm.host_rootfs;
    new_argv[n++] = "--preserve-env";
    if (g_ltm.trace_syscalls) {
        new_argv[n++] = "-s";
    }

    if (is_script) {
        new_argv[n++] = shebang_interp;
        if (shebang_arg[0] != '\0') {
            new_argv[n++] = shebang_arg;
        }
        new_argv[n++] = (char *)pathname;
    } else {
        new_argv[n++] = (char *)pathname;
    }

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
    if (!pathname) return -LINUX_EFAULT;
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
    if (!pathname) return -LINUX_EFAULT;
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
    if (!pathname) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) return 0;

    int ret = access(host_path, mode & (R_OK | W_OK | X_OK));
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath) {
    if (!oldpath || !newpath) return -LINUX_EFAULT;
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
    if (!oldpath || !newpath) return -LINUX_EFAULT;
    if (flags & 1 /* RENAME_NOREPLACE */) {
        char new_host[1024];
        int res = sandbox_resolve_path(newdirfd, newpath, new_host, sizeof(new_host), 0);
        if (res == 0) {
            struct stat st;
            if (lstat(new_host, &st) == 0) {
                return -LINUX_EEXIST;
            }
        }
    }
    return handle_renameat(olddirfd, oldpath, newdirfd, newpath);
}

int64_t handle_symlinkat(const char *target, int newdirfd, const char *linkpath) {
    if (!target || !linkpath) return -LINUX_EFAULT;
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
    if (!oldpath || !linkpath) return -LINUX_EFAULT;
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
    if (!pathname) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) return 0;

    int ret = chmod(host_path, mode);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_fchmod(int fd, mode_t mode) {
    if (fd < 0) return -LINUX_EBADF;
    int ret = fchmod(fd, mode);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group, int flags) {
    (void)flags;
    if (!pathname) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) return 0;

    if (access(host_path, F_OK) != 0) {
        return -LINUX_ENOENT;
    }

    int ret = chown(host_path, owner, group);
    if (ret < 0 && errno != EPERM && errno != EACCES) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_fchown(int fd, uid_t owner, gid_t group) {
    if (fd < 0) return -LINUX_EBADF;
    if (fcntl(fd, F_GETFL) < 0) return -LINUX_EBADF;
    int ret = fchown(fd, owner, group);
    if (ret < 0 && errno != EPERM && errno != EACCES) {
        return darwin_to_linux_errno(errno);
    }
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

    if (!target) {
        int ret = futimens(dirfd, ptimes);
        if (ret < 0) return darwin_to_linux_errno(errno);
        return 0;
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
    if (pid < 0 || (pid > 0 && pid != getpid())) return -LINUX_ESRCH;
    if (cpusetsize == 0) return -LINUX_EINVAL;
    if (!mask) return -LINUX_EFAULT;

    int ncpu = 8;
    size_t size = sizeof(ncpu);
    sysctlbyname("hw.ncpu", &ncpu, &size, NULL, 0);
    if (ncpu <= 0) ncpu = 8;

    memset(mask, 0, cpusetsize);
    for (int i = 0; i < ncpu && i < (int)(cpusetsize * 8); i++) {
        mask[i / 64] |= (1ULL << (i % 64));
    }
    return sizeof(unsigned long);
}

int64_t handle_sched_setaffinity(pid_t pid, size_t cpusetsize, const unsigned long *mask) {
    if (pid < 0 || (pid > 0 && pid != getpid())) return -LINUX_ESRCH;
    if (cpusetsize == 0) return -LINUX_EINVAL;
    if (!mask) return -LINUX_EFAULT;
    return 0;
}

/* Wave 1 Syscalls */

int64_t handle_statfs(const char *pathname, struct linux_statfs *buf) {
    if (!pathname || !buf) return -LINUX_EFAULT;

    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, pathname, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) {
        strncpy(host_path, g_ltm.host_rootfs, sizeof(host_path) - 1);
        host_path[sizeof(host_path) - 1] = '\0';
    }

    struct statfs dst;
    if (statfs(host_path, &dst) != 0) {
        return darwin_to_linux_errno(errno);
    }

    memset(buf, 0, sizeof(*buf));
    if (res == 1 || (pathname && (strcmp(pathname, "/proc") == 0 || strncmp(pathname, "/proc/", 6) == 0))) {
        buf->f_type = 0x9FA0; /* PROC_SUPER_MAGIC */
    } else if (pathname && (strcmp(pathname, "/sys") == 0 || strncmp(pathname, "/sys/", 5) == 0)) {
        buf->f_type = 0x62656572; /* SYSFS_MAGIC */
    } else {
        buf->f_type = 0xEF53; /* EXT4_SUPER_MAGIC */
    }
    buf->f_bsize = dst.f_bsize > 0 ? dst.f_bsize : 4096;
    buf->f_blocks = dst.f_blocks;
    buf->f_bfree = dst.f_bfree;
    buf->f_bavail = dst.f_bavail;
    buf->f_files = dst.f_files;
    buf->f_ffree = dst.f_ffree;
    buf->f_fsid.val[0] = dst.f_fsid.val[0];
    buf->f_fsid.val[1] = dst.f_fsid.val[1];
    buf->f_namelen = 255;
    buf->f_frsize = dst.f_bsize > 0 ? dst.f_bsize : 4096;
    buf->f_flags = 0;
    return 0;
}

int64_t handle_fstatfs(int fd, struct linux_statfs *buf) {
    if (!buf) return -LINUX_EFAULT;
    if (fd < 0) return -LINUX_EBADF;

    struct statfs dst;
    if (fstatfs(fd, &dst) != 0) {
        return darwin_to_linux_errno(errno);
    }

    memset(buf, 0, sizeof(*buf));
    char fd_path[1024];
    if (fcntl(fd, F_GETPATH, fd_path) == 0) {
        if (strstr(fd_path, "/proc") != NULL) {
            buf->f_type = 0x9FA0; /* PROC_SUPER_MAGIC */
        } else if (strstr(fd_path, "/sys") != NULL) {
            buf->f_type = 0x62656572; /* SYSFS_MAGIC */
        } else {
            buf->f_type = 0xEF53; /* EXT4_SUPER_MAGIC */
        }
    } else {
        buf->f_type = 0xEF53; /* EXT4_SUPER_MAGIC */
    }
    buf->f_bsize = dst.f_bsize > 0 ? dst.f_bsize : 4096;
    buf->f_blocks = dst.f_blocks;
    buf->f_bfree = dst.f_bfree;
    buf->f_bavail = dst.f_bavail;
    buf->f_files = dst.f_files;
    buf->f_ffree = dst.f_ffree;
    buf->f_fsid.val[0] = dst.f_fsid.val[0];
    buf->f_fsid.val[1] = dst.f_fsid.val[1];
    buf->f_namelen = 255;
    buf->f_frsize = dst.f_bsize > 0 ? dst.f_bsize : 4096;
    buf->f_flags = 0;
    return 0;
}

int64_t handle_truncate(const char *path, off_t length) {
    if (!path) return -LINUX_EFAULT;
    if (length < 0) return -LINUX_EINVAL;

    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) return -LINUX_EACCES;

    if (truncate(host_path, length) != 0) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_ftruncate(int fd, off_t length) {
    if (fd < 0) return -LINUX_EBADF;
    if (length < 0) return -LINUX_EINVAL;

    if (ftruncate(fd, length) != 0) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_fsync(int fd) {
    if (fd < 0) return -LINUX_EBADF;
    if (fsync(fd) != 0) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_fdatasync(int fd) {
    if (fd < 0) return -LINUX_EBADF;
    if (fsync(fd) != 0) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_sched_yield(void) {
    sched_yield();
    return 0;
}

int64_t handle_times(struct linux_tms *buf) {
    struct tms dt;
    clock_t ticks = times(&dt);
    if (ticks == (clock_t)-1) {
        return darwin_to_linux_errno(errno);
    }

    if (buf != NULL) {
        if ((uintptr_t)buf < 0x1000) return -LINUX_EFAULT;
        buf->tms_utime = dt.tms_utime;
        buf->tms_stime = dt.tms_stime;
        buf->tms_cutime = dt.tms_cutime;
        buf->tms_cstime = dt.tms_cstime;
    }
    return (int64_t)ticks;
}

int64_t handle_getsid(pid_t pid) {
    if (pid < 0) return -LINUX_EINVAL;
    if (pid > 0 && pid != getpid()) return -LINUX_ESRCH;
    pid_t ret = getsid(0);
    if (ret <= 0) ret = getpid();
    return ret;
}

int64_t handle_setsid(void) {
    pid_t ret = setsid();
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_getrusage(int who, struct linux_rusage *usage) {
    if (!usage) return -LINUX_EFAULT;
    if (who != 0 && who != -1 && who != 1) return -LINUX_EINVAL;

    int d_who = (who == 1) ? RUSAGE_SELF : who;
    struct rusage dru;
    memset(&dru, 0, sizeof(dru));
    if (getrusage(d_who, &dru) != 0) {
        return darwin_to_linux_errno(errno);
    }

    memset(usage, 0, sizeof(*usage));
    usage->ru_utime.tv_sec = dru.ru_utime.tv_sec;
    usage->ru_utime.tv_usec = dru.ru_utime.tv_usec;
    usage->ru_stime.tv_sec = dru.ru_stime.tv_sec;
    usage->ru_stime.tv_usec = dru.ru_stime.tv_usec;
    usage->ru_maxrss = dru.ru_maxrss / 1024;
    usage->ru_ixrss = dru.ru_ixrss;
    usage->ru_idrss = dru.ru_idrss;
    usage->ru_isrss = dru.ru_isrss;
    usage->ru_minflt = dru.ru_minflt;
    usage->ru_majflt = dru.ru_majflt;
    usage->ru_nswap = dru.ru_nswap;
    usage->ru_inblock = dru.ru_inblock;
    usage->ru_oublock = dru.ru_oublock;
    usage->ru_msgsnd = dru.ru_msgsnd;
    usage->ru_msgrcv = dru.ru_msgrcv;
    usage->ru_nsignals = dru.ru_nsignals;
    usage->ru_nvcsw = dru.ru_nvcsw;
    usage->ru_nivcsw = dru.ru_nivcsw;
    return 0;
}

int64_t handle_umask(mode_t mask) {
    mode_t old = umask(mask & 0777);
    return (int64_t)(old & 0777);
}

int64_t handle_readahead(int fd, off_t offset, size_t count) {
    (void)offset; (void)count;
    if (fd < 0) return -LINUX_EBADF;
    if (fcntl(fd, F_GETFL) < 0) return -LINUX_EBADF;
    return 0;
}

int64_t handle_madvise(void *addr, size_t length, int advice) {
    if (!addr || ((uintptr_t)addr & 0x3fff) != 0) return -LINUX_EINVAL;
    if (advice < 0 || advice > 4) return -LINUX_EINVAL;
    int ret = madvise(addr, length, advice);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

/* Networking & Sockets ABI Translation */

struct linux_msghdr {
    void *msg_name;
    socklen_t msg_namelen;
    int __pad1;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    int msg_flags;
    int __pad2;
};

static int copy_linux_to_darwin_sockaddr(const void *laddr, socklen_t llen, struct sockaddr_storage *daddr, socklen_t *dlen) {
    if (!laddr || llen < 2) return -LINUX_EINVAL;
    uint16_t family = *(const uint16_t *)laddr;

    if (family == 1) { /* AF_UNIX */
        struct sockaddr_un *dun = (struct sockaddr_un *)daddr;
        memset(dun, 0, sizeof(*dun));
        dun->sun_family = AF_UNIX;
        const char *lpath = (const char *)laddr + 2;
        char resolved_path[1024];
        if (lpath[0] != '\0') {
            if (sandbox_resolve_path(LINUX_AT_FDCWD, lpath, resolved_path, sizeof(resolved_path), 0) == 0) {
                lpath = resolved_path;
            }
        }
        size_t path_len = strlen(lpath);
        if (path_len >= sizeof(dun->sun_path)) path_len = sizeof(dun->sun_path) - 1;
        memcpy(dun->sun_path, lpath, path_len);
        dun->sun_len = sizeof(struct sockaddr_un);
        *dlen = sizeof(struct sockaddr_un);
        return 0;
    } else if (family == 2) { /* AF_INET */
        struct sockaddr_in *din = (struct sockaddr_in *)daddr;
        memset(din, 0, sizeof(*din));
        din->sin_len = sizeof(struct sockaddr_in);
        din->sin_family = AF_INET;
        if (llen >= 4) memcpy(&din->sin_port, (const char *)laddr + 2, 2);
        if (llen >= 8) memcpy(&din->sin_addr, (const char *)laddr + 4, 4);
        *dlen = sizeof(struct sockaddr_in);
        return 0;
    } else if (family == 10) { /* Linux AF_INET6 -> Darwin AF_INET6 (30) */
        struct sockaddr_in6 *din6 = (struct sockaddr_in6 *)daddr;
        memset(din6, 0, sizeof(*din6));
        din6->sin6_len = sizeof(struct sockaddr_in6);
        din6->sin6_family = AF_INET6;
        if (llen >= 4) memcpy(&din6->sin6_port, (const char *)laddr + 2, 2);
        if (llen >= 8) memcpy(&din6->sin6_flowinfo, (const char *)laddr + 4, 4);
        if (llen >= 24) memcpy(&din6->sin6_addr, (const char *)laddr + 8, 16);
        if (llen >= 28) memcpy(&din6->sin6_scope_id, (const char *)laddr + 24, 4);
        *dlen = sizeof(struct sockaddr_in6);
        return 0;
    } else {
        size_t cplen = llen < sizeof(*daddr) ? llen : sizeof(*daddr);
        memcpy(daddr, laddr, cplen);
        *dlen = (socklen_t)cplen;
        return 0;
    }
}

static int copy_darwin_to_linux_sockaddr(const struct sockaddr_storage *daddr, socklen_t dlen, void *laddr, socklen_t *llen) {
    if (!laddr || !llen || *llen == 0) return 0;
    const struct sockaddr *sa = (const struct sockaddr *)daddr;

    if (sa->sa_family == AF_UNIX) {
        if (*llen >= 2) *(uint16_t *)laddr = 1;
        const struct sockaddr_un *dun = (const struct sockaddr_un *)daddr;
        const char *spath = dun->sun_path;
        size_t root_len = strlen(g_ltm.host_rootfs);
        if (root_len > 0 && strncmp(spath, g_ltm.host_rootfs, root_len) == 0 && spath[root_len] == '/') {
            spath += root_len;
        }
        size_t path_len = strlen(spath);
        size_t space = *llen > 2 ? *llen - 2 : 0;
        if (path_len >= space) path_len = space > 0 ? space - 1 : 0;
        memcpy((char *)laddr + 2, spath, path_len);
        ((char *)laddr + 2)[path_len] = '\0';
        *llen = (socklen_t)(2 + path_len + 1);
        return 0;
    } else if (sa->sa_family == AF_INET) {
        const struct sockaddr_in *din = (const struct sockaddr_in *)daddr;
        if (*llen >= 2) *(uint16_t *)laddr = 2;
        if (*llen >= 4) memcpy((char *)laddr + 2, &din->sin_port, 2);
        if (*llen >= 8) memcpy((char *)laddr + 4, &din->sin_addr, 4);
        if (*llen >= 16) memset((char *)laddr + 8, 0, 8);
        if (*llen > 16) *llen = 16;
        return 0;
    } else if (sa->sa_family == AF_INET6) {
        const struct sockaddr_in6 *din6 = (const struct sockaddr_in6 *)daddr;
        if (*llen >= 2) *(uint16_t *)laddr = 10;
        if (*llen >= 4) memcpy((char *)laddr + 2, &din6->sin6_port, 2);
        if (*llen >= 8) memcpy((char *)laddr + 4, &din6->sin6_flowinfo, 4);
        if (*llen >= 24) memcpy((char *)laddr + 8, &din6->sin6_addr, 16);
        if (*llen >= 28) memcpy((char *)laddr + 24, &din6->sin6_scope_id, 4);
        if (*llen > 28) *llen = 28;
        return 0;
    } else {
        size_t cplen = dlen < *llen ? dlen : *llen;
        memcpy(laddr, daddr, cplen);
        *llen = (socklen_t)cplen;
        return 0;
    }
}

static void translate_linux_to_darwin_sockopt(int level, int optname, int *dlevel, int *doptname) {
    if (level == 1) { /* LINUX_SOL_SOCKET */
        *dlevel = SOL_SOCKET;
        switch (optname) {
            case 1:  *doptname = SO_DEBUG; break;
            case 2:  *doptname = SO_REUSEADDR; break;
            case 3:  *doptname = SO_TYPE; break;
            case 4:  *doptname = SO_ERROR; break;
            case 5:  *doptname = SO_DONTROUTE; break;
            case 6:  *doptname = SO_BROADCAST; break;
            case 7:  *doptname = SO_SNDBUF; break;
            case 8:  *doptname = SO_RCVBUF; break;
            case 9:  *doptname = SO_KEEPALIVE; break;
            case 10: *doptname = SO_OOBINLINE; break;
            case 13: *doptname = SO_LINGER; break;
            case 15: *doptname = SO_REUSEPORT; break;
            case 18: *doptname = SO_RCVLOWAT; break;
            case 19: *doptname = SO_SNDLOWAT; break;
            case 20: *doptname = SO_RCVTIMEO; break;
            case 21: *doptname = SO_SNDTIMEO; break;
            case 29: *doptname = SO_TIMESTAMP; break; /* Linux SO_TIMESTAMP -> Darwin SO_TIMESTAMP (0x0400) */
            case 30: *doptname = SO_ACCEPTCONN; break;
            case 35: *doptname = SO_TIMESTAMP; break; /* Linux SO_TIMESTAMPNS -> Darwin SO_TIMESTAMP (0x0400) */
            case 37: *doptname = SO_TIMESTAMP; break; /* Linux SO_TIMESTAMPING */
            default: *doptname = optname; break;
        }
    } else if (level == 6) { /* IPPROTO_TCP */
        *dlevel = IPPROTO_TCP;
        *doptname = optname;
    } else if (level == 0) { /* IPPROTO_IP */
        *dlevel = IPPROTO_IP;
        switch (optname) {
            case 1:  *doptname = IP_TOS; break;
            case 2:  *doptname = IP_TTL; break;
            case 3:  *doptname = IP_HDRINCL; break;
            case 4:  *doptname = IP_OPTIONS; break;
            case 6:  *doptname = IP_RECVOPTS; break;
            case 7:  *doptname = IP_RETOPTS; break;
            case 8:  *doptname = IP_PKTINFO; break;
            case 12: *doptname = IP_RECVTTL; break;
            case 13: *doptname = IP_RECVTOS; break;
            case 32: *doptname = IP_MULTICAST_IF; break;
            case 33: *doptname = IP_MULTICAST_TTL; break;
            case 34: *doptname = IP_MULTICAST_LOOP; break;
            case 35: *doptname = IP_ADD_MEMBERSHIP; break;
            case 36: *doptname = IP_DROP_MEMBERSHIP; break;
            default: *doptname = optname; break;
        }
    } else {
        *dlevel = level;
        *doptname = optname;
    }
}

typedef struct {
    int guest_fd;
    int notify_fd;
    int protocol;
    uint32_t portid;
    uint32_t groups;
    bool in_use;
    uint8_t *rx_buf;
    size_t rx_len;
    size_t rx_cap;
} ltm_netlink_t;

#define MAX_NETLINK_SOCKS 32
static ltm_netlink_t g_netlink_socks[MAX_NETLINK_SOCKS];
static pthread_mutex_t g_netlink_mutex = PTHREAD_MUTEX_INITIALIZER;

bool ltm_netlink_is_netlink(int fd) {
    if (fd < 0) return false;
    bool is_nl = false;
    pthread_mutex_lock(&g_netlink_mutex);
    for (int i = 0; i < MAX_NETLINK_SOCKS; i++) {
        if (g_netlink_socks[i].in_use && g_netlink_socks[i].guest_fd == fd) {
            is_nl = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_netlink_mutex);
    return is_nl;
}

void ltm_netlink_close(int fd) {
    if (fd < 0) return;
    pthread_mutex_lock(&g_netlink_mutex);
    for (int i = 0; i < MAX_NETLINK_SOCKS; i++) {
        if (g_netlink_socks[i].in_use && g_netlink_socks[i].guest_fd == fd) {
            if (g_netlink_socks[i].notify_fd >= 0) {
                close(g_netlink_socks[i].notify_fd);
                g_netlink_socks[i].notify_fd = -1;
            }
            if (g_netlink_socks[i].rx_buf) {
                free(g_netlink_socks[i].rx_buf);
                g_netlink_socks[i].rx_buf = NULL;
            }
            g_netlink_socks[i].rx_len = 0;
            g_netlink_socks[i].rx_cap = 0;
            g_netlink_socks[i].in_use = false;
            break;
        }
    }
    pthread_mutex_unlock(&g_netlink_mutex);
}

static int64_t ltm_netlink_create(int protocol, int type) {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) != 0) {
        return darwin_to_linux_errno(errno);
    }

    int bufsize = 131072;
    setsockopt(sv[0], SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
    setsockopt(sv[1], SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));

    int fl0 = fcntl(sv[0], F_GETFL, 0);
    fcntl(sv[0], F_SETFL, fl0 | O_NONBLOCK);
    int fl1 = fcntl(sv[1], F_GETFL, 0);
    fcntl(sv[1], F_SETFL, fl1 | O_NONBLOCK);

    if (type & 02000000 /* LINUX_SOCK_CLOEXEC */) {
        fcntl(sv[0], F_SETFD, FD_CLOEXEC);
        fcntl(sv[1], F_SETFD, FD_CLOEXEC);
    }

    pthread_mutex_lock(&g_netlink_mutex);
    int slot = -1;
    for (int i = 0; i < MAX_NETLINK_SOCKS; i++) {
        if (!g_netlink_socks[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot >= 0) {
        g_netlink_socks[slot].in_use = true;
        g_netlink_socks[slot].guest_fd = sv[0];
        g_netlink_socks[slot].notify_fd = sv[1];
        g_netlink_socks[slot].protocol = protocol;
        g_netlink_socks[slot].portid = (uint32_t)getpid();
        g_netlink_socks[slot].groups = 0;
        g_netlink_socks[slot].rx_cap = 65536;
        g_netlink_socks[slot].rx_buf = (uint8_t *)malloc(g_netlink_socks[slot].rx_cap);
        g_netlink_socks[slot].rx_len = 0;
    }
    pthread_mutex_unlock(&g_netlink_mutex);

    return sv[0];
}

static int ltm_netlink_bind(int fd, const void *addr, socklen_t len) {
    if (!addr || len < 2) return -LINUX_EINVAL;
    const struct linux_sockaddr_nl *snl = (const struct linux_sockaddr_nl *)addr;
    if (snl->nl_family != LINUX_AF_NETLINK) return -LINUX_EINVAL;

    pthread_mutex_lock(&g_netlink_mutex);
    for (int i = 0; i < MAX_NETLINK_SOCKS; i++) {
        if (g_netlink_socks[i].in_use && g_netlink_socks[i].guest_fd == fd) {
            g_netlink_socks[i].portid = snl->nl_pid ? snl->nl_pid : (uint32_t)getpid();
            g_netlink_socks[i].groups = snl->nl_groups;
            break;
        }
    }
    pthread_mutex_unlock(&g_netlink_mutex);
    return 0;
}

static int ltm_netlink_getsockname(int fd, void *addr, socklen_t *len) {
    if (!addr || !len || *len < sizeof(struct linux_sockaddr_nl)) return -LINUX_EINVAL;
    struct linux_sockaddr_nl *snl = (struct linux_sockaddr_nl *)addr;
    memset(snl, 0, sizeof(*snl));
    snl->nl_family = LINUX_AF_NETLINK;

    pthread_mutex_lock(&g_netlink_mutex);
    for (int i = 0; i < MAX_NETLINK_SOCKS; i++) {
        if (g_netlink_socks[i].in_use && g_netlink_socks[i].guest_fd == fd) {
            snl->nl_pid = g_netlink_socks[i].portid;
            snl->nl_groups = g_netlink_socks[i].groups;
            break;
        }
    }
    pthread_mutex_unlock(&g_netlink_mutex);
    *len = sizeof(struct linux_sockaddr_nl);
    return 0;
}

#define DARWIN_ROUNDUP(a) ((a) > 0 ? (1 + (((a) - 1) | (sizeof(uint32_t) - 1))) : sizeof(uint32_t))

static int ltm_get_default_gateway(struct in_addr *gw, char *out_ifname, size_t ifnamesz) {
    int mib[6] = {CTL_NET, PF_ROUTE, 0, AF_INET, NET_RT_DUMP, 0};
    size_t needed = 0;
    if (sysctl(mib, 6, NULL, &needed, NULL, 0) < 0 || needed == 0) return -1;
    char *buf = (char *)malloc(needed);
    if (!buf) return -1;
    if (sysctl(mib, 6, buf, &needed, NULL, 0) < 0) {
        free(buf);
        return -1;
    }

    char *next = buf;
    char *lim = buf + needed;
    int found = 0;

    while (next < lim) {
        struct rt_msghdr *rtm = (struct rt_msghdr *)next;
        if (rtm->rtm_msglen == 0) break;
        if ((rtm->rtm_flags & RTF_GATEWAY) && (rtm->rtm_flags & RTF_UP)) {
            char *cp = (char *)(rtm + 1);
            struct sockaddr *sa_dst = NULL;
            struct sockaddr *sa_gw = NULL;

            for (int i = 0; i < RTAX_MAX; i++) {
                if (rtm->rtm_addrs & (1 << i)) {
                    struct sockaddr *sa = (struct sockaddr *)cp;
                    if (i == RTAX_DST) sa_dst = sa;
                    else if (i == RTAX_GATEWAY) sa_gw = sa;
                    cp += DARWIN_ROUNDUP(sa->sa_len);
                }
            }

            if (sa_dst && sa_gw && sa_gw->sa_family == AF_INET) {
                struct sockaddr_in *sin_dst = (struct sockaddr_in *)sa_dst;
                struct sockaddr_in *sin_gw = (struct sockaddr_in *)sa_gw;
                if (sa_dst->sa_len == 0 || sin_dst->sin_addr.s_addr == 0) {
                    if (gw) *gw = sin_gw->sin_addr;
                    if (out_ifname && ifnamesz > 0) {
                        char dname[32] = {0};
                        if_indextoname(rtm->rtm_index, dname);
                        darwin_to_linux_ifname(dname, out_ifname, ifnamesz);
                    }
                    found = 1;
                    break;
                }
            }
        }
        next += rtm->rtm_msglen;
    }

    free(buf);
    return found ? 0 : -1;
}

static void nl_add_done(char *resp_buf, size_t *resp_len, uint32_t seq, uint32_t pid) {
    struct linux_nlmsghdr *done = (struct linux_nlmsghdr *)(resp_buf + *resp_len);
    done->nlmsg_len = LINUX_NLMSG_LENGTH(sizeof(int));
    done->nlmsg_type = LINUX_NLMSG_DONE;
    done->nlmsg_flags = LINUX_NLM_F_MULTI;
    done->nlmsg_seq = seq;
    done->nlmsg_pid = pid;
    int *done_err = (int *)LINUX_NLMSG_DATA(done);
    *done_err = 0;
    *resp_len += LINUX_NLMSG_ALIGN(done->nlmsg_len);
}

static void nl_add_rtattr(struct linux_nlmsghdr *nlh, size_t maxlen, uint16_t type, const void *data, size_t len) {
    size_t rta_len = LINUX_RTA_LENGTH(len);
    size_t new_len = LINUX_NLMSG_ALIGN(nlh->nlmsg_len) + LINUX_RTA_ALIGN(rta_len);
    if (new_len > maxlen) return;

    struct linux_rtattr *rta = (struct linux_rtattr *)(((char *)nlh) + LINUX_NLMSG_ALIGN(nlh->nlmsg_len));
    rta->rta_type = type;
    rta->rta_len = (uint16_t)rta_len;
    if (len > 0 && data) {
        memcpy(LINUX_RTA_DATA(rta), data, len);
    }
    size_t pad = LINUX_RTA_ALIGN(rta_len) - rta_len;
    if (pad > 0) {
        memset(((char *)rta) + rta_len, 0, pad);
    }
    nlh->nlmsg_len = (uint32_t)new_len;
}

ssize_t ltm_netlink_handle_send(int fd, const void *buf, size_t len) {
    if (!buf || len < sizeof(struct linux_nlmsghdr)) return -LINUX_EINVAL;

    const struct linux_nlmsghdr *req = (const struct linux_nlmsghdr *)buf;
    uint32_t seq = req->nlmsg_seq;
    uint32_t pid = req->nlmsg_pid ? req->nlmsg_pid : (uint32_t)getpid();
    uint16_t type = req->nlmsg_type;

    ltm_netif_info_t ifaces[16];
    int if_count = ltm_collect_netifs(ifaces, 16);

    char resp_buf[65536];
    size_t resp_len = 0;

    if (type == LINUX_RTM_GETLINK) {
        for (int i = 0; i < if_count; i++) {
            ltm_netif_info_t *it = &ifaces[i];
            size_t msg_start = resp_len;
            struct linux_nlmsghdr *nlh = (struct linux_nlmsghdr *)(resp_buf + msg_start);
            nlh->nlmsg_len = LINUX_NLMSG_LENGTH(sizeof(struct linux_ifinfomsg));
            nlh->nlmsg_type = LINUX_RTM_NEWLINK;
            nlh->nlmsg_flags = LINUX_NLM_F_MULTI;
            nlh->nlmsg_seq = seq;
            nlh->nlmsg_pid = pid;

            struct linux_ifinfomsg *ifi = (struct linux_ifinfomsg *)LINUX_NLMSG_DATA(nlh);
            ifi->ifi_family = 0;
            ifi->__ifi_pad = 0;
            ifi->ifi_type = it->arphdr_type;
            ifi->ifi_index = it->ifindex;
            ifi->ifi_flags = (uint32_t)(uint16_t)it->flags;
            ifi->ifi_change = 0;

            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFLA_IFNAME, it->linux_name, strlen(it->linux_name) + 1);
            uint32_t txqlen = 1000;
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFLA_TXQLEN, &txqlen, sizeof(txqlen));
            uint8_t opstate = (it->flags & LINUX_IFF_UP) ? 6 /* IF_OPER_UP */ : 2 /* IF_OPER_DOWN */;
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFLA_OPERSTATE, &opstate, sizeof(opstate));
            uint8_t linkmode = 0;
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFLA_LINKMODE, &linkmode, sizeof(linkmode));
            uint32_t mtu = it->mtu;
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFLA_MTU, &mtu, sizeof(mtu));

            if (it->has_mac) {
                nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFLA_ADDRESS, it->mac, 6);
                unsigned char bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
                nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFLA_BROADCAST, bcast, 6);
            }

            struct linux_rtnl_link_stats stats;
            memset(&stats, 0, sizeof(stats));
            stats.rx_bytes = (uint32_t)it->rx_bytes;
            stats.rx_packets = (uint32_t)it->rx_packets;
            stats.rx_errors = (uint32_t)it->rx_errors;
            stats.rx_dropped = (uint32_t)it->rx_dropped;
            stats.tx_bytes = (uint32_t)it->tx_bytes;
            stats.tx_packets = (uint32_t)it->tx_packets;
            stats.tx_errors = (uint32_t)it->tx_errors;
            stats.collisions = (uint32_t)it->collisions;
            stats.multicast = (uint32_t)it->multicast;
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFLA_STATS, &stats, sizeof(stats));

            resp_len += LINUX_NLMSG_ALIGN(nlh->nlmsg_len);
        }

        nl_add_done(resp_buf, &resp_len, seq, pid);

    } else if (type == LINUX_RTM_GETADDR) {
        for (int i = 0; i < if_count; i++) {
            ltm_netif_info_t *it = &ifaces[i];
            if (!it->has_ipv4) continue;

            size_t msg_start = resp_len;
            struct linux_nlmsghdr *nlh = (struct linux_nlmsghdr *)(resp_buf + msg_start);
            nlh->nlmsg_len = LINUX_NLMSG_LENGTH(sizeof(struct linux_ifaddrmsg));
            nlh->nlmsg_type = LINUX_RTM_NEWADDR;
            nlh->nlmsg_flags = LINUX_NLM_F_MULTI;
            nlh->nlmsg_seq = seq;
            nlh->nlmsg_pid = pid;

            struct linux_ifaddrmsg *ifa = (struct linux_ifaddrmsg *)LINUX_NLMSG_DATA(nlh);
            ifa->ifa_family = 2; /* AF_INET */
            uint32_t mask = ntohl(it->netmask.s_addr);
            int pfx = 0;
            while (mask & 0x80000000) { pfx++; mask <<= 1; }
            ifa->ifa_prefixlen = pfx;
            ifa->ifa_flags = 0x80; /* IFA_F_PERMANENT */
            ifa->ifa_scope = (strcmp(it->linux_name, "lo") == 0) ? 254 /* RT_SCOPE_HOST */ : 0 /* RT_SCOPE_UNIVERSE */;
            ifa->ifa_index = it->ifindex;

            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFA_ADDRESS, &it->ip, 4);
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFA_LOCAL, &it->ip, 4);
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFA_LABEL, it->linux_name, strlen(it->linux_name) + 1);
            if (strcmp(it->linux_name, "lo") != 0 && it->broadcast.s_addr != 0) {
                nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFA_BROADCAST, &it->broadcast, 4);
            }
            uint32_t ifa_flags = 0x80;
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFA_FLAGS, &ifa_flags, sizeof(ifa_flags));
            struct linux_ifa_cacheinfo ci = { 0xffffffff, 0xffffffff, 0, 0 };
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_IFA_CACHEINFO, &ci, sizeof(ci));

            resp_len += LINUX_NLMSG_ALIGN(nlh->nlmsg_len);
        }

        nl_add_done(resp_buf, &resp_len, seq, pid);

    } else if (type == LINUX_RTM_GETROUTE) {
        struct in_addr gw = {0};
        char gw_ifname[LINUX_IFNAMSIZ] = {0};
        bool has_gw = (ltm_get_default_gateway(&gw, gw_ifname, sizeof(gw_ifname)) == 0);

        /* 1. Default route if available */
        if (has_gw) {
            int gw_ifindex = 1;
            struct in_addr src_ip = {0};
            for (int i = 0; i < if_count; i++) {
                if (strcmp(ifaces[i].linux_name, gw_ifname) == 0) {
                    gw_ifindex = ifaces[i].ifindex;
                    src_ip = ifaces[i].ip;
                    break;
                }
            }
            if (gw_ifindex == 1 && if_count > 1) {
                gw_ifindex = ifaces[1].ifindex;
                src_ip = ifaces[1].ip;
            }

            size_t msg_start = resp_len;
            struct linux_nlmsghdr *nlh = (struct linux_nlmsghdr *)(resp_buf + msg_start);
            nlh->nlmsg_len = LINUX_NLMSG_LENGTH(sizeof(struct linux_rtmsg));
            nlh->nlmsg_type = LINUX_RTM_NEWROUTE;
            nlh->nlmsg_flags = LINUX_NLM_F_MULTI;
            nlh->nlmsg_seq = seq;
            nlh->nlmsg_pid = pid;

            struct linux_rtmsg *rt = (struct linux_rtmsg *)LINUX_NLMSG_DATA(nlh);
            rt->rtm_family = 2; /* AF_INET */
            rt->rtm_dst_len = 0;
            rt->rtm_src_len = 0;
            rt->rtm_tos = 0;
            rt->rtm_table = LINUX_RT_TABLE_MAIN;
            rt->rtm_protocol = LINUX_RTPROT_BOOT;
            rt->rtm_scope = LINUX_RT_SCOPE_UNIVERSE;
            rt->rtm_type = LINUX_RTN_UNICAST;
            rt->rtm_flags = 0;

            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_RTA_GATEWAY, &gw, sizeof(gw));
            uint32_t oif = gw_ifindex;
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_RTA_OIF, &oif, sizeof(oif));
            if (src_ip.s_addr != 0) {
                nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_RTA_PREFSRC, &src_ip, sizeof(src_ip));
            }

            resp_len += LINUX_NLMSG_ALIGN(nlh->nlmsg_len);
        }

        /* 2. Subnet routes for each interface with IPv4 */
        for (int i = 0; i < if_count; i++) {
            ltm_netif_info_t *it = &ifaces[i];
            if (!it->has_ipv4) continue;
            if (strcmp(it->linux_name, "lo") == 0) continue;

            uint32_t mask = ntohl(it->netmask.s_addr);
            int pfx = 0;
            while (mask & 0x80000000) { pfx++; mask <<= 1; }
            struct in_addr subnet;
            subnet.s_addr = it->ip.s_addr & it->netmask.s_addr;

            size_t msg_start = resp_len;
            struct linux_nlmsghdr *nlh = (struct linux_nlmsghdr *)(resp_buf + msg_start);
            nlh->nlmsg_len = LINUX_NLMSG_LENGTH(sizeof(struct linux_rtmsg));
            nlh->nlmsg_type = LINUX_RTM_NEWROUTE;
            nlh->nlmsg_flags = LINUX_NLM_F_MULTI;
            nlh->nlmsg_seq = seq;
            nlh->nlmsg_pid = pid;

            struct linux_rtmsg *rt = (struct linux_rtmsg *)LINUX_NLMSG_DATA(nlh);
            rt->rtm_family = 2; /* AF_INET */
            rt->rtm_dst_len = pfx;
            rt->rtm_src_len = 0;
            rt->rtm_tos = 0;
            rt->rtm_table = LINUX_RT_TABLE_MAIN;
            rt->rtm_protocol = LINUX_RTPROT_KERNEL;
            rt->rtm_scope = LINUX_RT_SCOPE_LINK;
            rt->rtm_type = LINUX_RTN_UNICAST;
            rt->rtm_flags = 0;

            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_RTA_DST, &subnet, sizeof(subnet));
            uint32_t oif = it->ifindex;
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_RTA_OIF, &oif, sizeof(oif));
            nl_add_rtattr(nlh, sizeof(resp_buf) - msg_start, LINUX_RTA_PREFSRC, &it->ip, sizeof(it->ip));

            resp_len += LINUX_NLMSG_ALIGN(nlh->nlmsg_len);
        }

        /* 3. Terminating DONE message */
        nl_add_done(resp_buf, &resp_len, seq, pid);

    } else {
        nl_add_done(resp_buf, &resp_len, seq, pid);
    }

    pthread_mutex_lock(&g_netlink_mutex);
    ltm_netlink_t *nl = NULL;
    for (int i = 0; i < MAX_NETLINK_SOCKS; i++) {
        if (g_netlink_socks[i].in_use && g_netlink_socks[i].guest_fd == fd) {
            nl = &g_netlink_socks[i];
            break;
        }
    }
    if (!nl) {
        pthread_mutex_unlock(&g_netlink_mutex);
        return -LINUX_EBADF;
    }

    if (resp_len > nl->rx_cap) {
        size_t new_cap = resp_len + 16384;
        uint8_t *new_b = (uint8_t *)realloc(nl->rx_buf, new_cap);
        if (new_b) {
            nl->rx_buf = new_b;
            nl->rx_cap = new_cap;
        }
    }
    if (resp_len <= nl->rx_cap) {
        memcpy(nl->rx_buf, resp_buf, resp_len);
        nl->rx_len = resp_len;
        char dummy[16];
        while (read(fd, dummy, sizeof(dummy)) > 0) {}
        char token = 1;
        write(nl->notify_fd, &token, 1);
    }
    pthread_mutex_unlock(&g_netlink_mutex);

    return (ssize_t)len;
}

int64_t ltm_netlink_recvmsg(int fd, void *msg, int flags) {
    if (!msg) return -LINUX_EINVAL;
    struct linux_msghdr *lmsg = (struct linux_msghdr *)msg;

    pthread_mutex_lock(&g_netlink_mutex);
    ltm_netlink_t *nl = NULL;
    for (int i = 0; i < MAX_NETLINK_SOCKS; i++) {
        if (g_netlink_socks[i].in_use && g_netlink_socks[i].guest_fd == fd) {
            nl = &g_netlink_socks[i];
            break;
        }
    }
    if (!nl) {
        pthread_mutex_unlock(&g_netlink_mutex);
        return -LINUX_EBADF;
    }

    if (nl->rx_len == 0) {
        pthread_mutex_unlock(&g_netlink_mutex);
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl & O_NONBLOCK) return -LINUX_EAGAIN;
        return 0;
    }

    size_t total_avail = nl->rx_len;
    size_t copied = 0;

    if (lmsg->msg_iov && lmsg->msg_iovlen > 0) {
        for (size_t i = 0; i < lmsg->msg_iovlen && copied < total_avail; i++) {
            size_t to_copy = lmsg->msg_iov[i].iov_len;
            if (to_copy > total_avail - copied) {
                to_copy = total_avail - copied;
            }
            if (to_copy > 0 && lmsg->msg_iov[i].iov_base) {
                memcpy(lmsg->msg_iov[i].iov_base, nl->rx_buf + copied, to_copy);
            }
            copied += to_copy;
        }
    }

    if (lmsg->msg_name && lmsg->msg_namelen >= sizeof(struct linux_sockaddr_nl)) {
        struct linux_sockaddr_nl *snl = (struct linux_sockaddr_nl *)lmsg->msg_name;
        memset(snl, 0, sizeof(*snl));
        snl->nl_family = LINUX_AF_NETLINK;
        snl->nl_pid = 0;
        lmsg->msg_namelen = sizeof(struct linux_sockaddr_nl);
    }

    lmsg->msg_flags = 0;
    if (copied < total_avail) {
        lmsg->msg_flags |= LINUX_MSG_TRUNC;
    }

    int64_t ret_val = (flags & LINUX_MSG_TRUNC) ? (int64_t)total_avail : (int64_t)copied;

    if (!(flags & LINUX_MSG_PEEK)) {
        nl->rx_len = 0;
        char dummy[16];
        while (read(fd, dummy, sizeof(dummy)) > 0) {}
    }

    pthread_mutex_unlock(&g_netlink_mutex);
    return ret_val;
}

int64_t ltm_netlink_recvfrom(int fd, void *buf, size_t len, int flags, void *src_addr, socklen_t *addrlen) {
    pthread_mutex_lock(&g_netlink_mutex);
    ltm_netlink_t *nl = NULL;
    for (int i = 0; i < MAX_NETLINK_SOCKS; i++) {
        if (g_netlink_socks[i].in_use && g_netlink_socks[i].guest_fd == fd) {
            nl = &g_netlink_socks[i];
            break;
        }
    }
    if (!nl) {
        pthread_mutex_unlock(&g_netlink_mutex);
        return -LINUX_EBADF;
    }

    if (nl->rx_len == 0) {
        pthread_mutex_unlock(&g_netlink_mutex);
        int fl = fcntl(fd, F_GETFL, 0);
        if (fl & O_NONBLOCK) return -LINUX_EAGAIN;
        return 0;
    }

    size_t total_avail = nl->rx_len;
    size_t to_copy = (len < total_avail) ? len : total_avail;
    if (to_copy > 0 && buf) {
        memcpy(buf, nl->rx_buf, to_copy);
    }

    if (src_addr && addrlen && *addrlen >= sizeof(struct linux_sockaddr_nl)) {
        struct linux_sockaddr_nl *snl = (struct linux_sockaddr_nl *)src_addr;
        memset(snl, 0, sizeof(*snl));
        snl->nl_family = LINUX_AF_NETLINK;
        snl->nl_pid = 0;
        *addrlen = sizeof(struct linux_sockaddr_nl);
    }

    int64_t ret_val = (flags & LINUX_MSG_TRUNC) ? (int64_t)total_avail : (int64_t)to_copy;

    if (!(flags & LINUX_MSG_PEEK)) {
        nl->rx_len = 0;
        char dummy[16];
        while (read(fd, dummy, sizeof(dummy)) > 0) {}
    }

    pthread_mutex_unlock(&g_netlink_mutex);
    return ret_val;
}

static int translate_linux_to_darwin_msg_flags(int lflags) {
    int dflags = 0;
    if (lflags & LINUX_MSG_OOB)       dflags |= MSG_OOB;
    if (lflags & LINUX_MSG_PEEK)      dflags |= MSG_PEEK;
    if (lflags & LINUX_MSG_DONTROUTE) dflags |= MSG_DONTROUTE;
    if (lflags & LINUX_MSG_EOR)       dflags |= MSG_EOR;
    if (lflags & LINUX_MSG_TRUNC)     dflags |= MSG_TRUNC;
    if (lflags & LINUX_MSG_CTRUNC)    dflags |= MSG_CTRUNC;
    if (lflags & LINUX_MSG_WAITALL)   dflags |= MSG_WAITALL;
    if (lflags & LINUX_MSG_DONTWAIT)  dflags |= MSG_DONTWAIT;
    return dflags;
}


int64_t handle_socket(int domain, int type, int protocol) {
    int ddomain = domain;
    if (domain == 1) ddomain = AF_UNIX;
    else if (domain == 2) ddomain = AF_INET;
    else if (domain == 10) ddomain = AF_INET6;
    else if (domain == 16) {
        return ltm_netlink_create(protocol, type);
    }

    int dtype = type & 0xf;
    int cloexec = (type & 02000000) != 0;
    int nonblock = (type & 00004000) != 0;

    int fd = socket(ddomain, dtype, protocol);
    if (fd < 0) return darwin_to_linux_errno(errno);

    if (fd >= 0 && fd < MAX_TRACKED_FDS) {
        g_sock_is_icmp[fd] = (protocol == 1 || protocol == 58);
    }

    if (cloexec) fcntl(fd, F_SETFD, FD_CLOEXEC);
    if (nonblock) {
        int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    }
    return fd;
}

int64_t handle_socketpair(int domain, int type, int protocol, int sv[2]) {
    if (!sv) return -LINUX_EINVAL;
    int ddomain = domain;
    if (domain == 1) ddomain = AF_UNIX;
    int dtype = type & 0xf;
    int cloexec = (type & 02000000) != 0;
    int nonblock = (type & 00004000) != 0;

    int fds[2];
    int ret = socketpair(ddomain, dtype, protocol, fds);
    if (ret < 0) return darwin_to_linux_errno(errno);

    if (cloexec) {
        fcntl(fds[0], F_SETFD, FD_CLOEXEC);
        fcntl(fds[1], F_SETFD, FD_CLOEXEC);
    }
    if (nonblock) {
        int fl0 = fcntl(fds[0], F_GETFL, 0);
        fcntl(fds[0], F_SETFL, fl0 | O_NONBLOCK);
        int fl1 = fcntl(fds[1], F_GETFL, 0);
        fcntl(fds[1], F_SETFL, fl1 | O_NONBLOCK);
    }

    sv[0] = fds[0];
    sv[1] = fds[1];
    return 0;
}

int64_t handle_connect(int fd, const void *addr, socklen_t len) {
    if (!addr) return -LINUX_EINVAL;
    struct sockaddr_storage daddr;
    socklen_t dlen = sizeof(daddr);
    int r = copy_linux_to_darwin_sockaddr(addr, len, &daddr, &dlen);
    if (r < 0) return r;

    int ret = connect(fd, (const struct sockaddr *)&daddr, dlen);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_bind(int fd, const void *addr, socklen_t len) {
    if (!addr) return -LINUX_EINVAL;
    if (ltm_netlink_is_netlink(fd)) {
        return ltm_netlink_bind(fd, addr, len);
    }
    struct sockaddr_storage daddr;
    socklen_t dlen = sizeof(daddr);
    int r = copy_linux_to_darwin_sockaddr(addr, len, &daddr, &dlen);
    if (r < 0) return r;

    int ret = bind(fd, (const struct sockaddr *)&daddr, dlen);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_listen(int fd, int backlog) {
    int ret = listen(fd, backlog);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_accept(int fd, void *addr, socklen_t *len) {
    if (fd < 0) return -LINUX_EBADF;
    if (addr && !len) return -LINUX_EFAULT;
    if (addr && len && *len > 0) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        int ret = accept(fd, (struct sockaddr *)&ss, &slen);
        if (ret < 0) return darwin_to_linux_errno(errno);

        socklen_t lnamelen = *len;
        copy_darwin_to_linux_sockaddr(&ss, slen, addr, &lnamelen);
        *len = lnamelen;
        return ret;
    }
    int ret = accept(fd, NULL, NULL);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_accept4(int fd, void *addr, socklen_t *len, int flags) {
    if (fd < 0) return -LINUX_EBADF;
    if (addr && !len) return -LINUX_EFAULT;
    if (flags & ~(02000000 | 00004000)) return -LINUX_EINVAL;

    int ret = handle_accept(fd, addr, len);
    if (ret < 0) return ret;
    int newfd = (int)ret;
    if (flags & 02000000) { /* LINUX_SOCK_CLOEXEC */
        fcntl(newfd, F_SETFD, FD_CLOEXEC);
    }
    if (flags & 00004000) { /* LINUX_SOCK_NONBLOCK */
        int fl = fcntl(newfd, F_GETFL, 0);
        fcntl(newfd, F_SETFL, fl | O_NONBLOCK);
    }
    return newfd;
}

int64_t handle_getsockname(int fd, void *addr, socklen_t *len) {
    if (!addr || !len) return -LINUX_EINVAL;
    if (ltm_netlink_is_netlink(fd)) {
        return ltm_netlink_getsockname(fd, addr, len);
    }
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int ret = getsockname(fd, (struct sockaddr *)&ss, &slen);
    if (ret < 0) return darwin_to_linux_errno(errno);

    socklen_t lnamelen = *len;
    copy_darwin_to_linux_sockaddr(&ss, slen, addr, &lnamelen);
    *len = lnamelen;
    return 0;
}

int64_t handle_getpeername(int fd, void *addr, socklen_t *len) {
    if (!addr || !len) return -LINUX_EINVAL;
    struct sockaddr_storage ss;
    socklen_t slen = sizeof(ss);
    int ret = getpeername(fd, (struct sockaddr *)&ss, &slen);
    if (ret < 0) return darwin_to_linux_errno(errno);

    socklen_t lnamelen = *len;
    copy_darwin_to_linux_sockaddr(&ss, slen, addr, &lnamelen);
    *len = lnamelen;
    return 0;
}

int64_t handle_sendto(int fd, const void *buf, size_t len, int flags, const void *dest_addr, socklen_t addrlen) {
    if (fd < 0) return -LINUX_EBADF;
    if (len > 0 && !buf) return -LINUX_EFAULT;
    if (ltm_netlink_is_netlink(fd)) {
        return ltm_netlink_handle_send(fd, buf, len);
    }
    int dflags = translate_linux_to_darwin_msg_flags(flags);
    if (dest_addr && addrlen > 0) {
        struct sockaddr_storage daddr;
        socklen_t dlen = sizeof(daddr);
        copy_linux_to_darwin_sockaddr(dest_addr, addrlen, &daddr, &dlen);
        ssize_t ret = sendto(fd, buf, len, dflags, (const struct sockaddr *)&daddr, dlen);
        if (ret < 0) return darwin_to_linux_errno(errno);
        return ret;
    }
    ssize_t ret = sendto(fd, buf, len, dflags, NULL, 0);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_recvfrom(int fd, void *buf, size_t len, int flags, void *src_addr, socklen_t *addrlen) {
    if (fd < 0) return -LINUX_EBADF;
    if (len > 0 && !buf) return -LINUX_EFAULT;
    if (ltm_netlink_is_netlink(fd)) {
        return ltm_netlink_recvfrom(fd, buf, len, flags, src_addr, addrlen);
    }

    int dflags = translate_linux_to_darwin_msg_flags(flags);
    ssize_t ret;

    if (src_addr && addrlen && *addrlen > 0) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        ret = recvfrom(fd, buf, len, dflags, (struct sockaddr *)&ss, &slen);
        if (ret < 0) return darwin_to_linux_errno(errno);

        socklen_t lnamelen = *addrlen;
        copy_darwin_to_linux_sockaddr(&ss, slen, src_addr, &lnamelen);
        *addrlen = lnamelen;
    } else {
        ret = recvfrom(fd, buf, len, dflags, NULL, NULL);
        if (ret < 0) return darwin_to_linux_errno(errno);
    }

    /* Darwin ICMP/raw sockets include the 20-byte IPv4 header in the recvfrom buffer,
     * whereas Linux SOCK_DGRAM IPPROTO_ICMP sockets deliver only the ICMP payload.
     * Strip the IPv4 header for Linux userspace ABI compatibility. */
    bool is_icmp = (fd >= 0 && fd < MAX_TRACKED_FDS && g_sock_is_icmp[fd]);
    if (buf && ret >= 20) {
        unsigned char *p = (unsigned char *)buf;
        if (is_icmp || ((p[0] >> 4) == 4 && p[9] == 1)) {
            size_t hlen = (p[0] & 0x0f) * 4;
            if (hlen >= 20 && (ssize_t)hlen <= ret) {
                memmove(buf, p + hlen, ret - hlen);
                ret -= hlen;
            }
        }
    }

    if (fd >= 0 && fd < MAX_TRACKED_FDS) {
        gettimeofday(&g_sock_last_timestamp[fd], NULL);
    }

    return ret;
}

int64_t handle_sendmsg(int fd, const void *msg, int flags) {
    if (!msg) return -LINUX_EINVAL;
    const struct linux_msghdr *lmsg = (const struct linux_msghdr *)msg;
    if (ltm_netlink_is_netlink(fd)) {
        if (lmsg->msg_iov && lmsg->msg_iovlen > 0) {
            return ltm_netlink_handle_send(fd, (const void *)lmsg->msg_iov[0].iov_base, lmsg->msg_iov[0].iov_len);
        }
        return 0;
    }

    struct msghdr dmsg;
    memset(&dmsg, 0, sizeof(dmsg));
    dmsg.msg_iov = lmsg->msg_iov;
    dmsg.msg_iovlen = (int)lmsg->msg_iovlen;
    dmsg.msg_control = lmsg->msg_control;
    dmsg.msg_controllen = (socklen_t)lmsg->msg_controllen;
    dmsg.msg_flags = lmsg->msg_flags;

    struct sockaddr_storage ss;
    socklen_t sslen = 0;
    if (lmsg->msg_name && lmsg->msg_namelen > 0) {
        copy_linux_to_darwin_sockaddr(lmsg->msg_name, lmsg->msg_namelen, &ss, &sslen);
        dmsg.msg_name = &ss;
        dmsg.msg_namelen = sslen;
    }

    int dflags = translate_linux_to_darwin_msg_flags(flags);
    ssize_t ret = sendmsg(fd, &dmsg, dflags);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_recvmsg(int fd, void *msg, int flags) {
    if (!msg) return -LINUX_EINVAL;
    if (ltm_netlink_is_netlink(fd)) {
        return ltm_netlink_recvmsg(fd, msg, flags);
    }

    struct linux_msghdr *lmsg = (struct linux_msghdr *)msg;

    struct msghdr dmsg;
    memset(&dmsg, 0, sizeof(dmsg));
    dmsg.msg_iov = lmsg->msg_iov;
    dmsg.msg_iovlen = (int)lmsg->msg_iovlen;

    char darwin_cbuf[512];
    if (lmsg->msg_control && lmsg->msg_controllen > 0) {
        dmsg.msg_control = darwin_cbuf;
        dmsg.msg_controllen = sizeof(darwin_cbuf);
    }

    struct sockaddr_storage ss;
    if (lmsg->msg_name && lmsg->msg_namelen > 0) {
        dmsg.msg_name = &ss;
        dmsg.msg_namelen = sizeof(ss);
    }

    int dflags = translate_linux_to_darwin_msg_flags(flags);
    ssize_t ret = recvmsg(fd, &dmsg, dflags);
    if (ret < 0) return darwin_to_linux_errno(errno);

    bool is_icmp = (fd >= 0 && fd < MAX_TRACKED_FDS && g_sock_is_icmp[fd]);
    if (lmsg->msg_iov && lmsg->msg_iovlen > 0 && ret >= 20) {
        unsigned char *p = (unsigned char *)lmsg->msg_iov[0].iov_base;
        if (p && (is_icmp || ((p[0] >> 4) == 4 && p[9] == 1))) {
            size_t hlen = (p[0] & 0x0f) * 4;
            if (hlen >= 20 && (ssize_t)hlen <= ret) {
                memmove(p, p + hlen, ret - hlen);
                ret -= hlen;
            }
        }
    }

    if (lmsg->msg_name && dmsg.msg_namelen > 0) {
        socklen_t lnamelen = lmsg->msg_namelen;
        copy_darwin_to_linux_sockaddr(&ss, dmsg.msg_namelen, lmsg->msg_name, &lnamelen);
        lmsg->msg_namelen = lnamelen;
    }

    /* Record packet arrival timestamp for SIOCGSTAMP */
    struct timeval recv_tv;
    gettimeofday(&recv_tv, NULL);
    if (fd >= 0 && fd < MAX_TRACKED_FDS) {
        g_sock_last_timestamp[fd] = recv_tv;
    }

    /* Process control messages and synthesize/translate Linux SO_TIMESTAMP */
    if (lmsg->msg_control && lmsg->msg_controllen > 0) {
        bool have_ts = false;
        struct timeval ts_val = recv_tv;

        if (dmsg.msg_controllen > 0) {
            struct cmsghdr *dcmsg = CMSG_FIRSTHDR(&dmsg);
            while (dcmsg) {
                if (dcmsg->cmsg_level == SOL_SOCKET && dcmsg->cmsg_type == SCM_TIMESTAMP &&
                    dcmsg->cmsg_len >= (socklen_t)(sizeof(struct cmsghdr) + sizeof(struct timeval))) {
                    memcpy(&ts_val, CMSG_DATA(dcmsg), sizeof(struct timeval));
                    have_ts = true;
                    if (fd >= 0 && fd < MAX_TRACKED_FDS) {
                        g_sock_last_timestamp[fd] = ts_val;
                    }
                    break;
                }
                dcmsg = CMSG_NXTHDR(&dmsg, dcmsg);
            }
        }

        bool want_ts = (fd >= 0 && fd < MAX_TRACKED_FDS && g_sock_so_timestamp[fd]) || have_ts;
        if (want_ts && lmsg->msg_controllen >= 32) {
            struct {
                uint64_t cmsg_len;
                int32_t  cmsg_level;
                int32_t  cmsg_type;
                struct timeval tv;
            } __attribute__((packed)) lcmsg;

            lcmsg.cmsg_len = sizeof(lcmsg); /* 32 bytes on Linux ARM64 */
            lcmsg.cmsg_level = 1;           /* LINUX_SOL_SOCKET */
            lcmsg.cmsg_type = 29;           /* LINUX_SO_TIMESTAMP */
            lcmsg.tv = ts_val;

            memcpy(lmsg->msg_control, &lcmsg, sizeof(lcmsg));
            lmsg->msg_controllen = sizeof(lcmsg);
        } else if (!have_ts) {
            lmsg->msg_controllen = 0;
        }
    } else {
        lmsg->msg_controllen = 0;
    }
    lmsg->msg_flags = dmsg.msg_flags;

    return ret;
}

int64_t handle_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen) {
    if (fd < 0) return -LINUX_EBADF;
    if (optlen > 0 && !optval) return -LINUX_EFAULT;
    if (ltm_netlink_is_netlink(fd)) return 0;

    if (level == 1 && (optname == 29 || optname == 35 || optname == 37)) {
        int on = (optval && optlen >= sizeof(int)) ? *(const int *)optval : 1;
        if (fd >= 0 && fd < MAX_TRACKED_FDS) {
            g_sock_so_timestamp[fd] = (on != 0);
        }
    }

    int dlevel, doptname;
    translate_linux_to_darwin_sockopt(level, optname, &dlevel, &doptname);
    int ret = setsockopt(fd, dlevel, doptname, optval, optlen);
    if (ret < 0) {
        /* SO_TIMESTAMP emulation: Darwin kernel doesn't support SO_TIMESTAMP on raw/dgram ICMP,
         * so we emulate it in userspace via synthesized cmsg in recvmsg and SIOCGSTAMP ioctl */
        if (level == 1 && (optname == 29 || optname == 35 || optname == 37)) {
            return 0;
        }
        /* Benign IP metadata options that shouldn't fail applications if unsupported */
        if (level == 0 && (optname == 12 || optname == 13 || optname == 8 || optname == 11 || optname == 7 || optname == 6 || optname == 4)) {
            return 0;
        }
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen) {
    if (fd < 0) return -LINUX_EBADF;
    if (!optval || !optlen) return -LINUX_EFAULT;
    if (ltm_netlink_is_netlink(fd)) return 0;
    if (level == 1 && (optname == 29 || optname == 35 || optname == 37)) {
        if (*optlen >= sizeof(int)) {
            int on = (fd >= 0 && fd < MAX_TRACKED_FDS) ? g_sock_so_timestamp[fd] : 0;
            *(int *)optval = on;
            *optlen = sizeof(int);
            return 0;
        }
    }
    int dlevel, doptname;
    translate_linux_to_darwin_sockopt(level, optname, &dlevel, &doptname);
    int ret = getsockopt(fd, dlevel, doptname, optval, optlen);
    if (ret < 0) {
        if (level == 1 && (optname == 29 || optname == 35 || optname == 37)) {
            if (*optlen >= sizeof(int)) {
                *(int *)optval = (fd >= 0 && fd < MAX_TRACKED_FDS) ? g_sock_so_timestamp[fd] : 0;
                *optlen = sizeof(int);
                return 0;
            }
        }
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_shutdown(int fd, int how) {
    if (fd < 0) return -LINUX_EBADF;
    if (how < 0 || how > 2) return -LINUX_EINVAL;
    int ret = shutdown(fd, how);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

/* Timers & Itimers */
int64_t handle_setitimer(int which, const void *new_val, void *old_val) {
    if (which < 0 || which > 2) return -LINUX_EINVAL;
    if (!new_val) return -LINUX_EFAULT;
    int ret = setitimer(which, (const struct itimerval *)new_val, (struct itimerval *)old_val);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_getitimer(int which, void *curr_val) {
    if (which < 0 || which > 2) return -LINUX_EINVAL;
    if (!curr_val) return -LINUX_EFAULT;
    int ret = getitimer(which, (struct itimerval *)curr_val);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

/* Epoll Subsystem Emulation */
#define EPOLL_MAX_INSTANCES 64
#define EPOLL_MAX_FDS 256

struct ltm_epoll_item {
    int fd;
    uint32_t events;
    uint64_t data;
};

struct ltm_epoll_ctx {
    int in_use;
    int epfd;
    int pipe_w;
    int count;
    struct ltm_epoll_item items[EPOLL_MAX_FDS];
};

static struct ltm_epoll_ctx g_epoll_pool[EPOLL_MAX_INSTANCES];

void ltm_epoll_close(int fd) {
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (g_epoll_pool[i].in_use && g_epoll_pool[i].epfd == fd) {
            g_epoll_pool[i].in_use = 0;
            close(g_epoll_pool[i].pipe_w);
            break;
        }
    }
}

int64_t handle_epoll_create1(int flags) {
    if (flags & ~02000000 /* EPOLL_CLOEXEC */) return -LINUX_EINVAL;
    int slot = -1;

    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (!g_epoll_pool[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -LINUX_EMFILE;

    int p[2];
    if (pipe(p) < 0) return darwin_to_linux_errno(errno);
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);

    g_epoll_pool[slot].in_use = 1;
    g_epoll_pool[slot].epfd = p[0];
    g_epoll_pool[slot].pipe_w = p[1];
    g_epoll_pool[slot].count = 0;

    return p[0];
}

int64_t handle_epoll_ctl(int epfd, int op, int fd, void *event) {
    int slot = -1;
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (g_epoll_pool[i].in_use && g_epoll_pool[i].epfd == epfd) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -LINUX_EBADF;

    struct ltm_epoll_ctx *ctx = &g_epoll_pool[slot];

    uint32_t ev_mask = 0;
    uint64_t ev_data = 0;
    if (event) {
        ev_mask = *(uint32_t *)event;
        ev_data = *(uint64_t *)((char *)event + 8);
    }

    if (op == 1) { /* EPOLL_CTL_ADD */
        for (int i = 0; i < ctx->count; i++) {
            if (ctx->items[i].fd == fd) return -LINUX_EEXIST;
        }
        if (ctx->count >= EPOLL_MAX_FDS) return -LINUX_ENOMEM;
        ctx->items[ctx->count].fd = fd;
        ctx->items[ctx->count].events = ev_mask;
        ctx->items[ctx->count].data = ev_data;
        ctx->count++;
        return 0;
    } else if (op == 2) { /* EPOLL_CTL_DEL */
        int found = -1;
        for (int i = 0; i < ctx->count; i++) {
            if (ctx->items[i].fd == fd) {
                found = i;
                break;
            }
        }
        if (found < 0) return -LINUX_ENOENT;
        ctx->items[found] = ctx->items[ctx->count - 1];
        ctx->count--;
        return 0;
    } else if (op == 3) { /* EPOLL_CTL_MOD */
        for (int i = 0; i < ctx->count; i++) {
            if (ctx->items[i].fd == fd) {
                ctx->items[i].events = ev_mask;
                ctx->items[i].data = ev_data;
                return 0;
            }
        }
        return -LINUX_ENOENT;
    }

    return -LINUX_EINVAL;
}

int64_t handle_epoll_pwait(int epfd, void *events, int maxevents, int timeout, const void *sigmask) {
    (void)sigmask;
    if (!events || maxevents <= 0) return -LINUX_EINVAL;

    int slot = -1;
    for (int i = 0; i < EPOLL_MAX_INSTANCES; i++) {
        if (g_epoll_pool[i].in_use && g_epoll_pool[i].epfd == epfd) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -LINUX_EBADF;

    struct ltm_epoll_ctx *ctx = &g_epoll_pool[slot];
    if (ctx->count == 0) {
        if (timeout > 0) usleep(timeout * 1000);
        return 0;
    }

    struct pollfd pfds[EPOLL_MAX_FDS];
    int n_pfds = ctx->count;
    for (int i = 0; i < n_pfds; i++) {
        pfds[i].fd = ctx->items[i].fd;
        pfds[i].events = 0;
        if (ctx->items[i].events & 0x0001) pfds[i].events |= POLLIN;
        if (ctx->items[i].events & 0x0004) pfds[i].events |= POLLOUT;
        if (ctx->items[i].events & 0x0002) pfds[i].events |= POLLPRI;
        pfds[i].revents = 0;
    }

    int ret = poll(pfds, n_pfds, timeout);
    if (ret < 0) return darwin_to_linux_errno(errno);
    if (ret == 0) return 0;

    int out_count = 0;
    for (int i = 0; i < n_pfds && out_count < maxevents; i++) {
        if (pfds[i].revents != 0) {
            uint32_t rev = 0;
            if (pfds[i].revents & POLLIN)  rev |= 0x0001; /* EPOLLIN */
            if (pfds[i].revents & POLLOUT) rev |= 0x0004; /* EPOLLOUT */
            if (pfds[i].revents & POLLPRI) rev |= 0x0002; /* EPOLLPRI */
            if (pfds[i].revents & POLLERR) rev |= 0x0008; /* EPOLLERR */
            if (pfds[i].revents & POLLHUP) rev |= 0x0010; /* EPOLLHUP */

            char *dest = (char *)events + (out_count * 16);
            *(uint32_t *)dest = rev;
            *(uint32_t *)(dest + 4) = 0;
            *(uint64_t *)(dest + 8) = ctx->items[i].data;
            out_count++;
        }
    }

    return out_count;
}

/* ========================================================================= */
/* Wave 2 System Call Handlers                                               */
/* ========================================================================= */

int64_t handle_flock(int fd, int operation) {
    if (fd < 0) return -LINUX_EBADF;
    if (fcntl(fd, F_GETFL) < 0) return -LINUX_EBADF;
    int op = operation & ~LOCK_NB;
    if (op != LOCK_SH && op != LOCK_EX && op != LOCK_UN) return -LINUX_EINVAL;
    if (operation & ~(LOCK_SH | LOCK_EX | LOCK_NB | LOCK_UN)) return -LINUX_EINVAL;

    int ret = flock(fd, operation);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

#ifndef FALLOC_FL_KEEP_SIZE
#define FALLOC_FL_KEEP_SIZE      0x01
#endif
#ifndef FALLOC_FL_PUNCH_HOLE
#define FALLOC_FL_PUNCH_HOLE     0x02
#endif

int64_t handle_fallocate(int fd, int mode, off_t offset, off_t len) {
    if (fd < 0) return -LINUX_EBADF;
    if (offset < 0 || len <= 0) return -LINUX_EINVAL;
    if (offset + len < 0) return -LINUX_EFBIG;

    struct stat st;
    if (fstat(fd, &st) != 0) return darwin_to_linux_errno(errno);
    if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode) || S_ISCHR(st.st_mode)) return -LINUX_ESPIPE;
    if (!S_ISREG(st.st_mode)) return -LINUX_ESPIPE;

    int flags = fcntl(fd, F_GETFL);
    if (flags < 0) return darwin_to_linux_errno(errno);
    if ((flags & O_ACCMODE) == O_RDONLY) return -LINUX_EBADF;

    if (mode & FALLOC_FL_PUNCH_HOLE) {
        if (!(mode & FALLOC_FL_KEEP_SIZE)) return -LINUX_EINVAL;
        return -LINUX_EOPNOTSUPP;
    }

    if (mode & ~(FALLOC_FL_KEEP_SIZE)) {
        return -LINUX_EOPNOTSUPP;
    }

    /* Darwin preallocation */
    fstore_t fst;
    memset(&fst, 0, sizeof(fst));
    fst.fst_flags = F_ALLOCATEALL;
    fst.fst_posmode = F_PEOFPOSMODE;
    fst.fst_offset = offset;
    fst.fst_length = len;
    fst.fst_bytesalloc = 0;
    (void)fcntl(fd, F_PREALLOCATE, &fst);

    if (!(mode & FALLOC_FL_KEEP_SIZE)) {
        if (offset + len > st.st_size) {
            if (ftruncate(fd, offset + len) != 0) {
                return darwin_to_linux_errno(errno);
            }
        }
    }

    return 0;
}

int64_t handle_chroot(const char *path) {
    if (!path) return -LINUX_EFAULT;

    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    if (res == 1) return -LINUX_EACCES;

    struct stat st;
    if (stat(host_path, &st) != 0) return darwin_to_linux_errno(errno);
    if (!S_ISDIR(st.st_mode)) return -LINUX_ENOTDIR;

    char resolved[1024];
    if (!realpath(host_path, resolved)) return darwin_to_linux_errno(errno);

    size_t root_len = strlen(g_ltm.host_root);
    if (strncmp(resolved, g_ltm.host_root, root_len) != 0 ||
        (resolved[root_len] != '/' && resolved[root_len] != '\0')) {
        ltm_log_debug("Sandbox BLOCKED chroot escape: %s -> %s", path, resolved);
        return -LINUX_EACCES;
    }

    strncpy(g_ltm.host_rootfs, resolved, sizeof(g_ltm.host_rootfs) - 1);
    g_ltm.host_rootfs[sizeof(g_ltm.host_rootfs) - 1] = '\0';

    char proc_path[1024];
    snprintf(proc_path, sizeof(proc_path), "%s/proc", g_ltm.host_rootfs);
    mkdir(proc_path, 0755);

    return 0;
}

int64_t handle_pread64(int fd, void *buf, size_t count, off_t offset) {
    if (fd < 0) return -LINUX_EBADF;
    if (offset < 0) return -LINUX_EINVAL;
    if (count > 0 && !buf) return -LINUX_EFAULT;
    if (count == 0) return 0;

    struct stat st;
    if (fstat(fd, &st) != 0) return darwin_to_linux_errno(errno);
    if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode) || S_ISCHR(st.st_mode)) return -LINUX_ESPIPE;

    ssize_t ret = pread(fd, buf, count, offset);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_pwrite64(int fd, const void *buf, size_t count, off_t offset) {
    if (fd < 0) return -LINUX_EBADF;
    if (offset < 0) return -LINUX_EINVAL;
    if (count > 0 && !buf) return -LINUX_EFAULT;
    if (count == 0) return 0;

    struct stat st;
    if (fstat(fd, &st) != 0) return darwin_to_linux_errno(errno);
    if (S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode) || S_ISCHR(st.st_mode)) return -LINUX_ESPIPE;

    ssize_t ret = pwrite(fd, buf, count, offset);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

#define LINUX_SFD_CLOEXEC  02000000
#define LINUX_SFD_NONBLOCK 00004000

int64_t handle_signalfd4(int ufd, const uint64_t *mask, size_t sizemask, int flags) {
    if (!mask) return -LINUX_EFAULT;
    if (sizemask != 8) return -LINUX_EINVAL;
    if (flags & ~(LINUX_SFD_CLOEXEC | LINUX_SFD_NONBLOCK)) return -LINUX_EINVAL;
    if (ufd != -1 && ufd < 0) return -LINUX_EBADF;

    pthread_mutex_lock(&g_signalfd_mutex);
    if (ufd == -1) {
        int p[2];
        if (pipe(p) != 0) {
            pthread_mutex_unlock(&g_signalfd_mutex);
            return darwin_to_linux_errno(errno);
        }
        if (flags & LINUX_SFD_CLOEXEC) {
            fcntl(p[0], F_SETFD, FD_CLOEXEC);
            fcntl(p[1], F_SETFD, FD_CLOEXEC);
        }
        if (flags & LINUX_SFD_NONBLOCK) {
            int fl = fcntl(p[0], F_GETFL, 0);
            fcntl(p[0], F_SETFL, fl | O_NONBLOCK);
        }

        int slot = -1;
        for (int i = 0; i < MAX_SIGNALFDS; i++) {
            if (!g_signalfds[i].in_use) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            close(p[0]);
            close(p[1]);
            pthread_mutex_unlock(&g_signalfd_mutex);
            return -LINUX_EMFILE;
        }

        g_signalfds[slot].read_fd = p[0];
        g_signalfds[slot].write_fd = p[1];
        g_signalfds[slot].mask = *mask;
        g_signalfds[slot].in_use = true;
        pthread_mutex_unlock(&g_signalfd_mutex);
        return p[0];
    } else {
        if (fcntl(ufd, F_GETFD) < 0) {
            pthread_mutex_unlock(&g_signalfd_mutex);
            return -LINUX_EBADF;
        }
        int slot = -1;
        for (int i = 0; i < MAX_SIGNALFDS; i++) {
            if (g_signalfds[i].in_use && g_signalfds[i].read_fd == ufd) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            pthread_mutex_unlock(&g_signalfd_mutex);
            return -LINUX_EINVAL;
        }
        g_signalfds[slot].mask = *mask;
        if (flags & LINUX_SFD_CLOEXEC) {
            fcntl(g_signalfds[slot].read_fd, F_SETFD, FD_CLOEXEC);
        }
        if (flags & LINUX_SFD_NONBLOCK) {
            int fl = fcntl(g_signalfds[slot].read_fd, F_GETFL, 0);
            fcntl(g_signalfds[slot].read_fd, F_SETFL, fl | O_NONBLOCK);
        }
        pthread_mutex_unlock(&g_signalfd_mutex);
        return ufd;
    }
}

int64_t handle_sync(void) {
    sync();
    return 0;
}

int64_t handle_syncfs(int fd) {
    if (fd < 0) return -LINUX_EBADF;
    if (fcntl(fd, F_GETFD) < 0) return -LINUX_EBADF;
    int ret = fsync(fd);
    if (ret < 0 && errno == EINVAL) {
        return 0;
    }
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

#define LINUX_TFD_TIMER_ABSTIME (1 << 0)
#define LINUX_TFD_CLOEXEC       02000000
#define LINUX_TFD_NONBLOCK      00004000

static void timerfd_dispatch_cb(void *context) {
    int write_fd = (int)(intptr_t)context;
    uint64_t exp = 1;
    write(write_fd, &exp, sizeof(exp));
}

int64_t handle_timerfd_create(int clockid, int flags) {
    if (clockid != 0 && clockid != 1) return -LINUX_EINVAL;
    if (flags & ~(LINUX_TFD_CLOEXEC | LINUX_TFD_NONBLOCK)) return -LINUX_EINVAL;

    int p[2];
    if (pipe(p) != 0) return darwin_to_linux_errno(errno);

    if (flags & LINUX_TFD_CLOEXEC) {
        fcntl(p[0], F_SETFD, FD_CLOEXEC);
        fcntl(p[1], F_SETFD, FD_CLOEXEC);
    }
    if (flags & LINUX_TFD_NONBLOCK) {
        int fl = fcntl(p[0], F_GETFL, 0);
        fcntl(p[0], F_SETFL, fl | O_NONBLOCK);
    }

    pthread_mutex_lock(&g_timerfd_mutex);
    int slot = -1;
    for (int i = 0; i < MAX_TIMERFDS; i++) {
        if (!g_timerfds[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        close(p[0]);
        close(p[1]);
        pthread_mutex_unlock(&g_timerfd_mutex);
        return -LINUX_EMFILE;
    }

    g_timerfds[slot].read_fd = p[0];
    g_timerfds[slot].write_fd = p[1];
    g_timerfds[slot].clockid = clockid;
    memset(&g_timerfds[slot].spec, 0, sizeof(g_timerfds[slot].spec));
    g_timerfds[slot].timer = NULL;
    g_timerfds[slot].active = false;
    g_timerfds[slot].in_use = true;
    pthread_mutex_unlock(&g_timerfd_mutex);

    return p[0];
}

int64_t handle_timerfd_settime(int fd, int flags, const struct linux_itimerspec *new_value, struct linux_itimerspec *old_value) {
    if (fd < 0) return -LINUX_EBADF;
    if (!new_value) return -LINUX_EFAULT;
    if (flags & ~LINUX_TFD_TIMER_ABSTIME) return -LINUX_EINVAL;

    if (new_value->it_value.tv_nsec < 0 || new_value->it_value.tv_nsec >= 1000000000L ||
        new_value->it_interval.tv_nsec < 0 || new_value->it_interval.tv_nsec >= 1000000000L ||
        new_value->it_value.tv_sec < 0 || new_value->it_interval.tv_sec < 0) {
        return -LINUX_EINVAL;
    }

    if (fcntl(fd, F_GETFD) < 0) return -LINUX_EBADF;

    pthread_mutex_lock(&g_timerfd_mutex);
    int slot = -1;
    for (int i = 0; i < MAX_TIMERFDS; i++) {
        if (g_timerfds[i].in_use && g_timerfds[i].read_fd == fd) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_timerfd_mutex);
        return -LINUX_EINVAL;
    }

    timerfd_entry_t *entry = &g_timerfds[slot];
    if (old_value) {
        *old_value = entry->spec;
    }

    if (entry->timer) {
        dispatch_source_cancel(entry->timer);
        dispatch_release(entry->timer);
        entry->timer = NULL;
    }

    /* Drain pipe */
    char drain[128];
    int fl = fcntl(entry->read_fd, F_GETFL, 0);
    fcntl(entry->read_fd, F_SETFL, fl | O_NONBLOCK);
    while (read(entry->read_fd, drain, sizeof(drain)) > 0) {}
    fcntl(entry->read_fd, F_SETFL, fl);

    entry->spec = *new_value;

    if (new_value->it_value.tv_sec == 0 && new_value->it_value.tv_nsec == 0) {
        entry->active = false;
        pthread_mutex_unlock(&g_timerfd_mutex);
        return 0;
    }

    entry->active = true;

    struct timespec now;
    clockid_t clk = (entry->clockid == 0) ? CLOCK_REALTIME : CLOCK_MONOTONIC;
    clock_gettime(clk, &now);
    int64_t now_ns = (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;

    int64_t delay_ns = 0;
    if (flags & LINUX_TFD_TIMER_ABSTIME) {
        int64_t target_ns = (int64_t)new_value->it_value.tv_sec * 1000000000LL + new_value->it_value.tv_nsec;
        int64_t diff = target_ns - now_ns;
        if (diff <= 0) {
            delay_ns = 0;
        } else {
            delay_ns = diff;
        }
    } else {
        delay_ns = (int64_t)new_value->it_value.tv_sec * 1000000000LL + new_value->it_value.tv_nsec;
    }

    int64_t interval_ns = (int64_t)new_value->it_interval.tv_sec * 1000000000LL + new_value->it_interval.tv_nsec;

    entry->arm_time_ns = now_ns;
    entry->delay_ns = delay_ns;

    if (delay_ns == 0) {
        uint64_t exp = 1;
        write(entry->write_fd, &exp, sizeof(exp));
        if (interval_ns > 0) {
            entry->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
            dispatch_set_context(entry->timer, (void *)(intptr_t)entry->write_fd);
            dispatch_source_set_event_handler_f(entry->timer, timerfd_dispatch_cb);
            dispatch_source_set_timer(entry->timer, dispatch_time(DISPATCH_TIME_NOW, interval_ns), (uint64_t)interval_ns, 0);
            dispatch_resume(entry->timer);
        }
    } else {
        entry->timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0));
        dispatch_set_context(entry->timer, (void *)(intptr_t)entry->write_fd);
        dispatch_source_set_event_handler_f(entry->timer, timerfd_dispatch_cb);
        uint64_t rep = (interval_ns > 0) ? (uint64_t)interval_ns : DISPATCH_TIME_FOREVER;
        dispatch_source_set_timer(entry->timer, dispatch_time(DISPATCH_TIME_NOW, delay_ns), rep, 0);
        dispatch_resume(entry->timer);
    }

    pthread_mutex_unlock(&g_timerfd_mutex);
    return 0;
}

int64_t handle_timerfd_gettime(int fd, struct linux_itimerspec *curr_value) {
    if (fd < 0) return -LINUX_EBADF;
    if (!curr_value) return -LINUX_EFAULT;
    if (fcntl(fd, F_GETFD) < 0) return -LINUX_EBADF;

    pthread_mutex_lock(&g_timerfd_mutex);
    int slot = -1;
    for (int i = 0; i < MAX_TIMERFDS; i++) {
        if (g_timerfds[i].in_use && g_timerfds[i].read_fd == fd) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_timerfd_mutex);
        return -LINUX_EINVAL;
    }

    timerfd_entry_t *entry = &g_timerfds[slot];
    curr_value->it_interval = entry->spec.it_interval;

    if (!entry->active) {
        curr_value->it_value.tv_sec = 0;
        curr_value->it_value.tv_nsec = 0;
    } else {
        struct timespec now;
        clockid_t clk = (entry->clockid == 0) ? CLOCK_REALTIME : CLOCK_MONOTONIC;
        clock_gettime(clk, &now);
        int64_t now_ns = (int64_t)now.tv_sec * 1000000000LL + now.tv_nsec;
        int64_t expire_at = entry->arm_time_ns + entry->delay_ns;
        int64_t rem = expire_at - now_ns;
        if (rem <= 0) {
            curr_value->it_value.tv_sec = 0;
            curr_value->it_value.tv_nsec = 0;
        } else {
            curr_value->it_value.tv_sec = rem / 1000000000LL;
            curr_value->it_value.tv_nsec = rem % 1000000000LL;
        }
    }

    pthread_mutex_unlock(&g_timerfd_mutex);
    return 0;
}

int64_t handle_clock_nanosleep(int clockid, int flags, const struct linux_timespec *req, struct linux_timespec *rem) {
    if (!req) return -LINUX_EFAULT;
    if (req->tv_nsec < 0 || req->tv_nsec >= 1000000000L || req->tv_sec < 0) return -LINUX_EINVAL;
    if (clockid != 0 && clockid != 1) return -LINUX_EINVAL;
    if (flags & ~1) return -LINUX_EINVAL;

    if (flags & 1) {
        struct timespec now;
        clockid_t clk = (clockid == 0) ? CLOCK_REALTIME : CLOCK_MONOTONIC;
        clock_gettime(clk, &now);
        if (req->tv_sec < now.tv_sec || (req->tv_sec == now.tv_sec && req->tv_nsec <= now.tv_nsec)) {
            return 0;
        }
        struct timespec delta;
        delta.tv_sec = req->tv_sec - now.tv_sec;
        delta.tv_nsec = req->tv_nsec - now.tv_nsec;
        if (delta.tv_nsec < 0) {
            delta.tv_sec -= 1;
            delta.tv_nsec += 1000000000L;
        }
        int ret = nanosleep(&delta, NULL);
        if (ret != 0) return darwin_to_linux_errno(errno);
        return 0;
    } else {
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
}

#define LINUX_MFD_CLOEXEC       0x0001U
#define LINUX_MFD_ALLOW_SEALING 0x0002U
#define LINUX_MFD_HUGETLB       0x0004U

int64_t handle_memfd_create(const char *name, unsigned int flags) {
    if (!name) return -LINUX_EFAULT;
    size_t len = strlen(name);
    if (len > 249) return -LINUX_EINVAL;
    if (flags & ~(LINUX_MFD_CLOEXEC | LINUX_MFD_ALLOW_SEALING | LINUX_MFD_HUGETLB)) return -LINUX_EINVAL;

    char tmpl[] = "/tmp/ltm_memfd_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return darwin_to_linux_errno(errno);
    unlink(tmpl);

    if (flags & LINUX_MFD_CLOEXEC) {
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    return fd;
}

int64_t handle_copy_file_range(int fd_in, off_t *off_in, int fd_out, off_t *off_out, size_t len, unsigned int flags) {
    if (flags != 0) return -LINUX_EINVAL;
    if (fd_in < 0 || fd_out < 0) return -LINUX_EBADF;

    struct stat st_in, st_out;
    if (fstat(fd_in, &st_in) != 0 || fstat(fd_out, &st_out) != 0) {
        return darwin_to_linux_errno(errno);
    }
    if (!S_ISREG(st_in.st_mode) || !S_ISREG(st_out.st_mode)) {
        return -LINUX_EINVAL;
    }

    int fl_out = fcntl(fd_out, F_GETFL);
    if (fl_out < 0) return darwin_to_linux_errno(errno);
    if (fl_out & O_APPEND) return -LINUX_EBADF;

    if (off_in) {
        if ((uintptr_t)off_in < 0x1000) return -LINUX_EFAULT;
        if (*off_in < 0) return -LINUX_EINVAL;
        if (*off_in + (off_t)len < 0) return -LINUX_EOVERFLOW;
    }
    if (off_out) {
        if ((uintptr_t)off_out < 0x1000) return -LINUX_EFAULT;
        if (*off_out < 0) return -LINUX_EINVAL;
        if (*off_out + (off_t)len < 0) return -LINUX_EOVERFLOW;
    }

    if (len == 0) return 0;

    off_t cur_in = off_in ? *off_in : lseek(fd_in, 0, SEEK_CUR);
    off_t cur_out = off_out ? *off_out : lseek(fd_out, 0, SEEK_CUR);
    if (cur_in < 0 || cur_out < 0) return darwin_to_linux_errno(errno);

    char buf[65536];
    size_t total_copied = 0;

    while (total_copied < len) {
        size_t to_read = len - total_copied;
        if (to_read > sizeof(buf)) to_read = sizeof(buf);

        ssize_t nread = pread(fd_in, buf, to_read, cur_in);
        if (nread <= 0) {
            if (nread < 0 && total_copied == 0) return darwin_to_linux_errno(errno);
            break;
        }

        ssize_t nwritten = pwrite(fd_out, buf, nread, cur_out);
        if (nwritten < 0) {
            if (total_copied == 0) return darwin_to_linux_errno(errno);
            break;
        }

        cur_in += nwritten;
        cur_out += nwritten;
        total_copied += nwritten;
        if (nwritten < nread) break;
    }

    if (off_in) *off_in = cur_in;
    else lseek(fd_in, cur_in, SEEK_SET);

    if (off_out) *off_out = cur_out;
    else lseek(fd_out, cur_out, SEEK_SET);

    return (int64_t)total_copied;
}

#define LINUX_CLOSE_RANGE_UNSHARE (1U << 1)
#define LINUX_CLOSE_RANGE_CLOEXEC (1U << 2)

int64_t handle_close_range(unsigned int first, unsigned int last, unsigned int flags) {
    if (first > last) return -LINUX_EINVAL;
    if (flags & ~(LINUX_CLOSE_RANGE_UNSHARE | LINUX_CLOSE_RANGE_CLOEXEC)) return -LINUX_EINVAL;

    struct rlimit rl;
    unsigned int max_fd = 10240;
    if (getrlimit(RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur > 0) {
        max_fd = (unsigned int)rl.rlim_cur;
    }
    if (last > max_fd) last = max_fd;

    for (unsigned int fd = first; fd <= last; fd++) {
        if (fcntl((int)fd, F_GETFD) < 0) continue;
        if (flags & LINUX_CLOSE_RANGE_CLOEXEC) {
            fcntl((int)fd, F_SETFD, FD_CLOEXEC);
        } else {
            handle_close((int)fd);
        }
    }
    return 0;
}

#define LINUX_RESOLVE_NO_XDEV       0x01
#define LINUX_RESOLVE_NO_MAGICLINKS 0x02
#define LINUX_RESOLVE_NO_SYMLINKS   0x04
#define LINUX_RESOLVE_BENEATH       0x08
#define LINUX_RESOLVE_IN_ROOT       0x10
#define LINUX_RESOLVE_CACHED        0x20

int64_t handle_openat2(int dirfd, const char *pathname, struct linux_open_how *how, size_t size) {
    if (!pathname || !how) return -LINUX_EFAULT;
    if (size < 24) return -LINUX_EINVAL;
    if (how->resolve & ~(0x01 | 0x02 | 0x04 | 0x08 | 0x10 | 0x20)) return -LINUX_EINVAL;

    if (how->resolve & LINUX_RESOLVE_BENEATH) {
        if (strstr(pathname, "..")) {
            char base_path[1024];
            if (dirfd == AT_FDCWD || dirfd == LINUX_AT_FDCWD) {
                if (!getcwd(base_path, sizeof(base_path))) return -LINUX_EIO;
            } else {
                if (fcntl(dirfd, F_GETPATH, base_path) < 0) return -LINUX_EBADF;
            }
            char host_path[1024];
            int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0);
            if (res < 0) return res;
            if (res == 1) return -LINUX_EXDEV;
            size_t blen = strlen(base_path);
            if (strncmp(host_path, base_path, blen) != 0 ||
                (host_path[blen] != '/' && host_path[blen] != '\0')) {
                return -LINUX_EXDEV;
            }
        }
    }

    if (how->resolve & LINUX_RESOLVE_NO_SYMLINKS) {
        char host_path[1024];
        int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0x100);
        if (res < 0) return res;
        if (res == 0) {
            struct stat lst;
            if (lstat(host_path, &lst) == 0 && S_ISLNK(lst.st_mode)) {
                return -LINUX_ELOOP;
            }
        }
    }

    return handle_openat(dirfd, pathname, (int)how->flags, (mode_t)how->mode);
}

int64_t handle_faccessat2(int dirfd, const char *pathname, int mode, int flags) {
    if (!pathname) return -LINUX_EFAULT;
    if (mode & ~(R_OK | W_OK | X_OK)) return -LINUX_EINVAL;
    if (flags & ~(0x100 | 0x200 | 0x1000)) return -LINUX_EINVAL;

    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), flags);
    if (res < 0) return res;

    if (res == 1) {
        if (mode & W_OK) return -LINUX_EACCES;
        return 0;
    }

    int dflags = 0;
    if (flags & 0x100) dflags |= AT_SYMLINK_NOFOLLOW;
    if (flags & 0x200) dflags |= AT_EACCESS;

    int ret = faccessat(AT_FDCWD, host_path, mode, dflags);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

/* ========================================================================= */
/* Wave 3 System Call Handlers                                               */
/* ========================================================================= */

/* Extended Attributes (xattr) */
int64_t handle_setxattr(const char *path, const char *name, const void *value, size_t size, int flags) {
    if (!path || !name) return -LINUX_EFAULT;
    if (size > 0 && !value) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), 0);
    if (res < 0) return res;

    int dflags = 0;
    if (flags & 1 /* XATTR_CREATE */) dflags |= XATTR_CREATE;
    if (flags & 2 /* XATTR_REPLACE */) dflags |= XATTR_REPLACE;

    int ret = setxattr(host_path, name, value, size, 0, dflags);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_lsetxattr(const char *path, const char *name, const void *value, size_t size, int flags) {
    if (!path || !name) return -LINUX_EFAULT;
    if (size > 0 && !value) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), AT_SYMLINK_NOFOLLOW);
    if (res < 0) return res;

    int dflags = XATTR_NOFOLLOW;
    if (flags & 1) dflags |= XATTR_CREATE;
    if (flags & 2) dflags |= XATTR_REPLACE;

    int ret = setxattr(host_path, name, value, size, 0, dflags);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_fsetxattr(int fd, const char *name, const void *value, size_t size, int flags) {
    if (fd < 0) return -LINUX_EBADF;
    if (!name) return -LINUX_EFAULT;
    if (size > 0 && !value) return -LINUX_EFAULT;

    int dflags = 0;
    if (flags & 1) dflags |= XATTR_CREATE;
    if (flags & 2) dflags |= XATTR_REPLACE;

    int ret = fsetxattr(fd, name, value, size, 0, dflags);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_getxattr(const char *path, const char *name, void *value, size_t size) {
    if (!path || !name) return -LINUX_EFAULT;
    if (size > 0 && !value) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), 0);
    if (res < 0) return res;

    ssize_t ret = getxattr(host_path, name, value, size, 0, 0);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_lgetxattr(const char *path, const char *name, void *value, size_t size) {
    if (!path || !name) return -LINUX_EFAULT;
    if (size > 0 && !value) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), AT_SYMLINK_NOFOLLOW);
    if (res < 0) return res;

    ssize_t ret = getxattr(host_path, name, value, size, 0, XATTR_NOFOLLOW);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_fgetxattr(int fd, const char *name, void *value, size_t size) {
    if (fd < 0) return -LINUX_EBADF;
    if (!name) return -LINUX_EFAULT;
    if (size > 0 && !value) return -LINUX_EFAULT;

    ssize_t ret = fgetxattr(fd, name, value, size, 0, 0);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_listxattr(const char *path, char *list, size_t size) {
    if (!path) return -LINUX_EFAULT;
    if (size > 0 && !list) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), 0);
    if (res < 0) return res;

    ssize_t ret = listxattr(host_path, list, size, 0);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_llistxattr(const char *path, char *list, size_t size) {
    if (!path) return -LINUX_EFAULT;
    if (size > 0 && !list) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), AT_SYMLINK_NOFOLLOW);
    if (res < 0) return res;

    ssize_t ret = listxattr(host_path, list, size, XATTR_NOFOLLOW);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_flistxattr(int fd, char *list, size_t size) {
    if (fd < 0) return -LINUX_EBADF;
    if (size > 0 && !list) return -LINUX_EFAULT;

    ssize_t ret = flistxattr(fd, list, size, 0);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_removexattr(const char *path, const char *name) {
    if (!path || !name) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), 0);
    if (res < 0) return res;

    int ret = removexattr(host_path, name, 0);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_lremovexattr(const char *path, const char *name) {
    if (!path || !name) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(LINUX_AT_FDCWD, path, host_path, sizeof(host_path), AT_SYMLINK_NOFOLLOW);
    if (res < 0) return res;

    int ret = removexattr(host_path, name, XATTR_NOFOLLOW);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_fremovexattr(int fd, const char *name) {
    if (fd < 0) return -LINUX_EBADF;
    if (!name) return -LINUX_EFAULT;

    int ret = fremovexattr(fd, name, 0);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

/* Event & Advanced I/O */
#define LINUX_EFD_SEMAPHORE 1
#define LINUX_EFD_CLOEXEC   02000000
#define LINUX_EFD_NONBLOCK  00004000

int64_t handle_eventfd2(unsigned int initval, int flags) {
    if (flags & ~(LINUX_EFD_SEMAPHORE | LINUX_EFD_CLOEXEC | LINUX_EFD_NONBLOCK)) {
        return -LINUX_EINVAL;
    }
    int p[2];
    if (pipe(p) != 0) return darwin_to_linux_errno(errno);

    if (flags & LINUX_EFD_CLOEXEC) {
        fcntl(p[0], F_SETFD, FD_CLOEXEC);
        fcntl(p[1], F_SETFD, FD_CLOEXEC);
    }
    if (flags & LINUX_EFD_NONBLOCK) {
        int fl0 = fcntl(p[0], F_GETFL, 0);
        int fl1 = fcntl(p[1], F_GETFL, 0);
        fcntl(p[0], F_SETFL, fl0 | O_NONBLOCK);
        fcntl(p[1], F_SETFL, fl1 | O_NONBLOCK);
    }

    if (initval > 0) {
        uint64_t val = initval;
        write(p[1], &val, sizeof(val));
    }

    pthread_mutex_lock(&g_eventfd_mutex);
    for (int i = 0; i < MAX_EVENTFDS; i++) {
        if (!g_eventfds[i].in_use) {
            g_eventfds[i].read_fd = p[0];
            g_eventfds[i].write_fd = p[1];
            g_eventfds[i].flags = flags;
            g_eventfds[i].in_use = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_eventfd_mutex);

    return p[0];
}

int64_t handle_preadv(int fd, const struct linux_iovec *iov, int iovcnt, off_t offset) {
    if (fd < 0) return -LINUX_EBADF;
    if (iovcnt < 0 || iovcnt > 1024) return -LINUX_EINVAL;
    if (offset < 0) return -LINUX_EINVAL;
    if (iovcnt == 0) return 0;
    if (!iov) return -LINUX_EFAULT;

    ssize_t total = 0;
    off_t cur_off = offset;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        if (!iov[i].iov_base) return -LINUX_EFAULT;

        ssize_t nr = pread(fd, (void *)(uintptr_t)iov[i].iov_base, iov[i].iov_len, cur_off);
        if (nr <= 0) {
            if (nr < 0 && total == 0) return darwin_to_linux_errno(errno);
            break;
        }
        total += nr;
        cur_off += nr;
        if ((size_t)nr < iov[i].iov_len) break;
    }
    return total;
}

int64_t handle_pwritev(int fd, const struct linux_iovec *iov, int iovcnt, off_t offset) {
    if (fd < 0) return -LINUX_EBADF;
    if (iovcnt < 0 || iovcnt > 1024) return -LINUX_EINVAL;
    if (offset < 0) return -LINUX_EINVAL;
    if (iovcnt == 0) return 0;
    if (!iov) return -LINUX_EFAULT;

    ssize_t total = 0;
    off_t cur_off = offset;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len == 0) continue;
        if (!iov[i].iov_base) return -LINUX_EFAULT;

        ssize_t nw = pwrite(fd, (const void *)(uintptr_t)iov[i].iov_base, iov[i].iov_len, cur_off);
        if (nw <= 0) {
            if (nw < 0 && total == 0) return darwin_to_linux_errno(errno);
            break;
        }
        total += nw;
        cur_off += nw;
        if ((size_t)nw < iov[i].iov_len) break;
    }
    return total;
}

int64_t handle_sendfile(int out_fd, int in_fd, off_t *offset, size_t count) {
    if (out_fd < 0 || in_fd < 0) return -LINUX_EBADF;
    if (count == 0) return 0;

    off_t cur_off = offset ? *offset : lseek(in_fd, 0, SEEK_CUR);
    if (cur_off < 0) return darwin_to_linux_errno(errno);

    char buf[65536];
    size_t total = 0;
    while (total < count) {
        size_t chunk = count - total;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);

        ssize_t nr = pread(in_fd, buf, chunk, cur_off + total);
        if (nr <= 0) {
            if (nr < 0 && total == 0) return darwin_to_linux_errno(errno);
            break;
        }
        ssize_t nw = write(out_fd, buf, nr);
        if (nw < 0) {
            if (total == 0) return darwin_to_linux_errno(errno);
            break;
        }
        total += nw;
        if (nw < nr) break;
    }

    if (offset) {
        *offset += total;
    } else {
        lseek(in_fd, cur_off + total, SEEK_SET);
    }
    return total;
}

int64_t handle_splice(int fd_in, off_t *off_in, int fd_out, off_t *off_out, size_t len, unsigned int flags) {
    (void)flags;
    if (fd_in < 0 || fd_out < 0) return -LINUX_EBADF;
    if (len == 0) return 0;

    char buf[65536];
    size_t chunk = len > sizeof(buf) ? sizeof(buf) : len;
    ssize_t nr;
    if (off_in) {
        nr = pread(fd_in, buf, chunk, *off_in);
    } else {
        nr = read(fd_in, buf, chunk);
    }
    if (nr <= 0) {
        if (nr < 0) return darwin_to_linux_errno(errno);
        return 0;
    }

    ssize_t nw;
    if (off_out) {
        nw = pwrite(fd_out, buf, nr, *off_out);
    } else {
        nw = write(fd_out, buf, nr);
    }
    if (nw < 0) return darwin_to_linux_errno(errno);

    if (off_in) *off_in += nw;
    if (off_out) *off_out += nw;
    return nw;
}

int64_t handle_tee(int fd_in, int fd_out, size_t len, unsigned int flags) {
    (void)flags;
    if (fd_in < 0 || fd_out < 0) return -LINUX_EBADF;
    if (fd_in == fd_out) return -LINUX_EINVAL;
    if (len == 0) return 0;

    struct stat st_in, st_out;
    if (fstat(fd_in, &st_in) != 0) return darwin_to_linux_errno(errno);
    if (fstat(fd_out, &st_out) != 0) return darwin_to_linux_errno(errno);
    if (!S_ISFIFO(st_in.st_mode) || !S_ISFIFO(st_out.st_mode)) return -LINUX_EINVAL;

    char buf[65536];
    size_t chunk = len > sizeof(buf) ? sizeof(buf) : len;
    ssize_t nr = read(fd_in, buf, chunk);
    if (nr <= 0) {
        if (nr < 0) return darwin_to_linux_errno(errno);
        return 0;
    }
    ssize_t nw = write(fd_out, buf, nr);
    if (nw < 0) return darwin_to_linux_errno(errno);
    return nw;
}

int64_t handle_sync_file_range(int fd, off_t offset, off_t nbytes, unsigned int flags) {
    (void)flags;
    if (fd < 0) return -LINUX_EBADF;
    if (offset < 0 || nbytes < 0) return -LINUX_EINVAL;
    if (fcntl(fd, F_GETFD) < 0) return -LINUX_EBADF;
    fsync(fd);
    return 0;
}

/* Process, Clocks & Personality */
int64_t handle_personality(unsigned long persona) {
    (void)persona;
    return 0; /* PER_LINUX */
}

int64_t handle_clock_getres(int clockid, struct linux_timespec *res) {
    if (clockid < 0 || clockid > 15) return -LINUX_EINVAL;
    if (res) {
        res->tv_sec = 0;
        res->tv_nsec = 1; /* 1ns resolution on Apple Silicon */
    }
    return 0;
}

int64_t handle_getrlimit(int resource, struct linux_rlimit *rlim) {
    if (resource < 0 || resource > 15) return -LINUX_EINVAL;
    if (!rlim) return -LINUX_EFAULT;
    struct linux_rlimit64 l64;
    int ret = handle_prlimit64(0, resource, NULL, &l64);
    if (ret < 0) return ret;
    rlim->rlim_cur = l64.rlim_cur;
    rlim->rlim_max = l64.rlim_max;
    return 0;
}

int64_t handle_setrlimit(int resource, const struct linux_rlimit *rlim) {
    if (resource < 0 || resource > 15) return -LINUX_EINVAL;
    if (!rlim) return -LINUX_EFAULT;
    return 0;
}

static int s_keepcaps = 0;

int64_t handle_prctl(int option, unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5) {
    (void)arg3; (void)arg4; (void)arg5;
    /* PR_SET_NAME = 15 */
    if (option == 15) {
        if (!arg2) return -LINUX_EFAULT;
        pthread_setname_np((const char *)arg2);
        return 0;
    }
    /* PR_GET_NAME = 16 */
    if (option == 16) {
        if (!arg2) return -LINUX_EFAULT;
        pthread_getname_np(pthread_self(), (char *)arg2, 16);
        return 0;
    }
    /* PR_SET_DUMPABLE = 4, PR_GET_DUMPABLE = 3 */
    if (option == 3) return 1;
    if (option == 4) return 0;
    /* PR_SET_NO_NEW_PRIVS = 38, PR_GET_NO_NEW_PRIVS = 39 */
    if (option == 38) return 0;
    if (option == 39) return 0;
    /* PR_CAPBSET_READ = 23, PR_CAPBSET_DROP = 24 */
    if (option == 23) return 1;
    if (option == 24) return 0;
    /* PR_SET_KEEPCAPS = 8, PR_GET_KEEPCAPS = 7 */
    if (option == 7) return s_keepcaps;
    if (option == 8) {
        s_keepcaps = (arg2 != 0);
        return 0;
    }
    /* PR_CAP_AMBIENT = 47 */
    if (option == 47) return 0;
    /* PR_SET_PDEATHSIG = 1, PR_GET_PDEATHSIG = 2 */
    if (option == 1) return 0;
    if (option == 2) {
        if (!arg2) return -LINUX_EFAULT;
        *(int *)arg2 = 0;
        return 0;
    }
    /* PR_SET_CHILD_SUBREAPER = 36, PR_GET_CHILD_SUBREAPER = 37 */
    if (option == 36) return 0;
    if (option == 37) {
        if (!arg2) return -LINUX_EFAULT;
        *(int *)arg2 = 0;
        return 0;
    }
    /* PR_SET_SECUREBITS = 28, PR_GET_SECUREBITS = 27 */
    if (option == 27) return 0;
    if (option == 28) return 0;

    return -LINUX_EINVAL;
}

int64_t handle_getcpu(unsigned *cpu, unsigned *node, void *tcache) {
    (void)tcache;
    if (cpu) *cpu = 0;
    if (node) *node = 0;
    return 0;
}

/* Wave 4 System Call Handlers */

int64_t handle_mknodat(int dirfd, const char *pathname, mode_t mode, dev_t dev) {
    if (!pathname) return -LINUX_EFAULT;
    char host_path[1024];
    int res = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), 0);
    if (res < 0) return res;

    int ret;
    if (S_ISFIFO(mode)) {
        ret = mkfifo(host_path, mode & 0777);
    } else if (S_ISREG(mode) || (mode & S_IFMT) == 0) {
        int fd = open(host_path, O_CREAT | O_EXCL | O_WRONLY, mode & 0777);
        if (fd < 0) return darwin_to_linux_errno(errno);
        close(fd);
        return 0;
    } else {
        ret = mknod(host_path, mode, dev);
    }
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_vhangup(void) {
    return 0;
}

int64_t handle_waitid(int which, id_t id, struct linux_siginfo *infop, int options, void *ru) {
    (void)ru;
    if (which < 0 || which > 2) return -LINUX_EINVAL;
    if (options == 0) return -LINUX_EINVAL;

    idtype_t idtype = (idtype_t)which;
    siginfo_t dinfo;
    memset(&dinfo, 0, sizeof(dinfo));

    int doptions = 0;
    if (options & 1 /* WNOHANG */) doptions |= WNOHANG;
    if (options & 2 /* WSTOPPED */) doptions |= WUNTRACED;
    if (options & 4 /* WEXITED */) doptions |= WEXITED;
    if (options & 8 /* WCONTINUED */) doptions |= WCONTINUED;
    if (options & 0x01000000 /* WNOWAIT */) doptions |= WNOWAIT;

    int ret = waitid(idtype, id, &dinfo, doptions);
    if (ret != 0) return darwin_to_linux_errno(errno);

    if (infop) {
        memset(infop, 0, sizeof(*infop));
        infop->si_signo = darwin_to_linux_signal(dinfo.si_signo);
        infop->si_code = dinfo.si_code;
        infop->_sifields._sigchld._pid = dinfo.si_pid;
        infop->_sifields._sigchld._uid = dinfo.si_uid;
        if (WIFSIGNALED(dinfo.si_status)) {
            int d_sig = WTERMSIG(dinfo.si_status);
            int l_sig = darwin_to_linux_signal(d_sig);
            infop->_sifields._sigchld._status = (dinfo.si_status & ~0x7f) | (l_sig & 0x7f);
        } else if (WIFSTOPPED(dinfo.si_status)) {
            int d_sig = WSTOPSIG(dinfo.si_status);
            int l_sig = darwin_to_linux_signal(d_sig);
            infop->_sifields._sigchld._status = (dinfo.si_status & ~0xff00) | ((l_sig & 0xff) << 8);
        } else {
            infop->_sifields._sigchld._status = dinfo.si_status;
        }
    }
    return 0;
}

int64_t handle_sched_setparam(pid_t pid, const struct linux_sched_param *param) {
    if (pid < 0) return -LINUX_EINVAL;
    if (!param) return -LINUX_EFAULT;
    if (pid > 0 && kill(pid, 0) != 0 && errno == ESRCH) return -LINUX_ESRCH;
    return 0;
}

int64_t handle_sched_getparam(pid_t pid, struct linux_sched_param *param) {
    if (pid < 0) return -LINUX_EINVAL;
    if (!param) return -LINUX_EFAULT;
    if (pid > 0 && kill(pid, 0) != 0 && errno == ESRCH) return -LINUX_ESRCH;
    param->sched_priority = 0;
    return 0;
}

int64_t handle_sched_setscheduler(pid_t pid, int policy, const struct linux_sched_param *param) {
    if (pid < 0 || policy < 0 || policy > 6) return -LINUX_EINVAL;
    if (!param) return -LINUX_EFAULT;
    if (pid > 0 && kill(pid, 0) != 0 && errno == ESRCH) return -LINUX_ESRCH;
    return 0;
}

int64_t handle_sched_getscheduler(pid_t pid) {
    if (pid < 0) return -LINUX_EINVAL;
    if (pid > 0 && kill(pid, 0) != 0 && errno == ESRCH) return -LINUX_ESRCH;
    return 0; /* SCHED_OTHER */
}

int64_t handle_sched_get_priority_max(int policy) {
    if (policy < 0 || policy > 6) return -LINUX_EINVAL;
    if (policy == 1 /* SCHED_FIFO */ || policy == 2 /* SCHED_RR */) return 99;
    return 0;
}

int64_t handle_sched_get_priority_min(int policy) {
    if (policy < 0 || policy > 6) return -LINUX_EINVAL;
    if (policy == 1 /* SCHED_FIFO */ || policy == 2 /* SCHED_RR */) return 1;
    return 0;
}

int64_t handle_sched_rr_get_interval(pid_t pid, struct linux_timespec *tp) {
    if (pid < 0) return -LINUX_EINVAL;
    if (!tp) return -LINUX_EFAULT;
    if (pid > 0 && kill(pid, 0) != 0 && errno == ESRCH) return -LINUX_ESRCH;
    tp->tv_sec = 0;
    tp->tv_nsec = 100000000; /* 100ms standard timeslice */
    return 0;
}

int64_t handle_rt_sigpending(void *set, size_t sigsetsize) {
    if (sigsetsize != 8) return -LINUX_EINVAL;
    if (!set) return -LINUX_EFAULT;

    sigset_t dset;
    if (sigpending(&dset) != 0) return darwin_to_linux_errno(errno);

    uint64_t lset = 0;
    for (int i = 1; i <= 32; i++) {
        if (sigismember(&dset, i)) {
            lset |= (1ULL << (i - 1));
        }
    }
    *(uint64_t *)set = lset;
    return 0;
}

int64_t handle_setpriority(int which, int who, int prio) {
    if (which < 0 || which > 2) return -LINUX_EINVAL;
    if (setpriority(which, (id_t)who, prio) != 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_getpriority(int which, int who) {
    if (which < 0 || which > 2) return -LINUX_EINVAL;
    errno = 0;
    int r = getpriority(which, (id_t)who);
    if (r == -1 && errno != 0) return darwin_to_linux_errno(errno);
    return 20 - r; /* Linux returns 20 - nice value */
}

int64_t handle_setfsuid(uid_t fsuid) {
    (void)fsuid;
    return 0;
}

int64_t handle_setfsgid(gid_t fsgid) {
    (void)fsgid;
    return 0;
}

int64_t handle_sethostname(const char *name, size_t len) {
    if (!name) return -LINUX_EFAULT;
    if (len > 64) return -LINUX_EINVAL;
    memset(g_hostname, 0, sizeof(g_hostname));
    memcpy(g_hostname, name, len);
    g_hostname[len] = '\0';
    return 0;
}

int64_t handle_setdomainname(const char *name, size_t len) {
    if (!name) return -LINUX_EFAULT;
    if (len > 64) return -LINUX_EINVAL;
    memset(g_domainname, 0, sizeof(g_domainname));
    memcpy(g_domainname, name, len);
    g_domainname[len] = '\0';
    return 0;
}

int64_t handle_mremap(unsigned long old_address, unsigned long old_size, unsigned long new_size, unsigned long flags, unsigned long new_address) {
    if ((old_address & 0x3FFF) != 0) return -LINUX_EINVAL;
    if (old_size == 0 || new_size == 0) return -LINUX_EINVAL;
    if (flags & ~(1UL | 2UL | 4UL)) return -LINUX_EINVAL;
    if ((flags & 2UL /* MREMAP_FIXED */) && !(flags & 1UL /* MREMAP_MAYMOVE */)) return -LINUX_EINVAL;

    size_t aligned_old = (old_size + 0x3FFF) & ~0x3FFFULL;
    size_t aligned_new = (new_size + 0x3FFF) & ~0x3FFFULL;

    if (flags & 2UL /* MREMAP_FIXED */) {
        if ((new_address & 0x3FFF) != 0) return -LINUX_EINVAL;
        if (new_address == old_address) return -LINUX_EINVAL;
    }

    if (aligned_new == aligned_old) {
        if (flags & 2UL) {
            void *new_addr = mmap((void *)new_address, aligned_new, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
            if (new_addr == MAP_FAILED) return darwin_to_linux_errno(errno);
            memcpy(new_addr, (const void *)old_address, old_size);
            if (!(flags & 4UL /* MREMAP_DONTUNMAP */)) munmap((void *)old_address, aligned_old);
            return (int64_t)(uintptr_t)new_addr;
        }
        return old_address;
    }

    if (aligned_new < aligned_old) {
        /* Shrinking */
        munmap((void *)(old_address + aligned_new), aligned_old - aligned_new);
        return old_address;
    }

    /* Growing: attempt in-place expansion if not MREMAP_FIXED */
    if (!(flags & 2UL)) {
        mach_vm_address_t check_addr = (mach_vm_address_t)(old_address + aligned_old);
        mach_vm_size_t check_size = 0;
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t object_name;
        kern_return_t kr = mach_vm_region(mach_task_self(), &check_addr, &check_size,
                                          VM_REGION_BASIC_INFO_64, (vm_region_info_t)&info, &count, &object_name);
        bool can_grow_in_place = (kr != KERN_SUCCESS || check_addr >= (mach_vm_address_t)(old_address + aligned_new));

        if (can_grow_in_place) {
            void *p = mmap((void *)(old_address + aligned_old), aligned_new - aligned_old,
                           PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
            if (p != MAP_FAILED) {
                return old_address;
            }
        }
    }

    if (!(flags & 1UL /* MREMAP_MAYMOVE */)) {
        return -LINUX_ENOMEM;
    }

    void *target_dest = NULL;
    int map_flags = MAP_ANON | MAP_PRIVATE;
    if (flags & 2UL /* MREMAP_FIXED */) {
        target_dest = (void *)new_address;
        map_flags |= MAP_FIXED;
    }

    void *new_addr = mmap(target_dest, aligned_new, PROT_READ | PROT_WRITE, map_flags, -1, 0);
    if (new_addr == MAP_FAILED) return darwin_to_linux_errno(errno);

    memcpy(new_addr, (const void *)old_address, old_size);
    if (!(flags & 4UL /* MREMAP_DONTUNMAP */)) {
        munmap((void *)old_address, aligned_old);
    }
    return (int64_t)(uintptr_t)new_addr;
}

int64_t handle_fadvise64(int fd, off_t offset, off_t len, int advice) {
    (void)offset; (void)len;
    if (fd < 0) return -LINUX_EBADF;
    if (fcntl(fd, F_GETFD) < 0) return -LINUX_EBADF;
    if (advice < 0 || advice > 5) return -LINUX_EINVAL;
    return 0;
}

int64_t handle_msync(unsigned long start, size_t len, int flags) {
    if ((start & 0x3FFF) != 0) return -LINUX_EINVAL;
    if (len == 0) return 0;

    int dflags = 0;
    if (flags & 1) dflags |= MS_ASYNC;
    if (flags & 2) dflags |= MS_INVALIDATE;
    if (flags & 4) dflags |= MS_SYNC;

    size_t aligned_len = (len + 0x3FFF) & ~0x3FFFULL;
    int r = msync((void *)start, aligned_len, dflags);
    if (r != 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_mlock(unsigned long addr, size_t len) {
    if ((addr & 0x3FFF) != 0) return -LINUX_EINVAL;
    if (len == 0) return 0;
    size_t aligned_len = (len + 0x3FFF) & ~0x3FFFULL;
    if (mlock((const void *)addr, aligned_len) != 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_munlock(unsigned long addr, size_t len) {
    if ((addr & 0x3FFF) != 0) return -LINUX_EINVAL;
    if (len == 0) return 0;
    size_t aligned_len = (len + 0x3FFF) & ~0x3FFFULL;
    if (munlock((const void *)addr, aligned_len) != 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_mlockall(int flags) {
    if (flags <= 0 || (flags & ~3)) return -LINUX_EINVAL;
    return 0;
}

int64_t handle_munlockall(void) {
    return 0;
}

int64_t handle_mincore(unsigned long start, size_t len, unsigned char *vec) {
    if ((start & 0x3FFF) != 0) return -LINUX_EINVAL;
    if (!vec) return -LINUX_EFAULT;
    if (len == 0) return 0;

    size_t aligned_len = (len + 0x3FFF) & ~0x3FFFULL;
    if (mincore((const void *)start, aligned_len, (char *)vec) != 0) return darwin_to_linux_errno(errno);
    return 0;
}

/* ========================================================================= */
/*                         WAVE 5 SYSCALL HANDLERS                          */
/* ========================================================================= */

/* Inotify Subsystem */
#define MAX_INOTIFY_INSTANCES 32
#define MAX_INOTIFY_WATCHES 64

struct ltm_inotify_watch {
    int wd;
    uint32_t mask;
    char path[PATH_MAX];
};

struct ltm_inotify_instance {
    bool active;
    int read_fd;
    int write_fd;
    int flags;
    int next_wd;
    struct ltm_inotify_watch watches[MAX_INOTIFY_WATCHES];
    int watch_count;
};

static struct ltm_inotify_instance g_inotifys[MAX_INOTIFY_INSTANCES];
static pthread_mutex_t g_inotify_mutex = PTHREAD_MUTEX_INITIALIZER;

int64_t handle_inotify_init1(int flags) {
    if (flags & ~(0x800 | 0x80000)) { /* IN_NONBLOCK | IN_CLOEXEC */
        return -LINUX_EINVAL;
    }

    pthread_mutex_lock(&g_inotify_mutex);
    int slot = -1;
    for (int i = 0; i < MAX_INOTIFY_INSTANCES; i++) {
        if (!g_inotifys[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        pthread_mutex_unlock(&g_inotify_mutex);
        return -LINUX_EMFILE;
    }

    int p[2];
    if (pipe(p) != 0) {
        pthread_mutex_unlock(&g_inotify_mutex);
        return darwin_to_linux_errno(errno);
    }

    if (flags & 0x800) { /* IN_NONBLOCK */
        fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
        fcntl(p[1], F_SETFL, fcntl(p[1], F_GETFL) | O_NONBLOCK);
    }
    if (flags & 0x80000) { /* IN_CLOEXEC */
        fcntl(p[0], F_SETFD, FD_CLOEXEC);
        fcntl(p[1], F_SETFD, FD_CLOEXEC);
    }

    g_inotifys[slot].active = true;
    g_inotifys[slot].read_fd = p[0];
    g_inotifys[slot].write_fd = p[1];
    g_inotifys[slot].flags = flags;
    g_inotifys[slot].next_wd = 1;
    g_inotifys[slot].watch_count = 0;

    pthread_mutex_unlock(&g_inotify_mutex);
    return p[0];
}

int64_t handle_inotify_add_watch(int fd, const char *pathname, uint32_t mask) {
    if (fd < 0) return -LINUX_EBADF;
    if (!pathname) return -LINUX_EFAULT;
    if (mask == 0) return -LINUX_EINVAL;

    pthread_mutex_lock(&g_inotify_mutex);
    struct ltm_inotify_instance *inst = NULL;
    for (int i = 0; i < MAX_INOTIFY_INSTANCES; i++) {
        if (g_inotifys[i].active && g_inotifys[i].read_fd == fd) {
            inst = &g_inotifys[i];
            break;
        }
    }
    if (!inst) {
        pthread_mutex_unlock(&g_inotify_mutex);
        return -LINUX_EBADF;
    }

    char host_path[PATH_MAX];
    int r = sandbox_resolve_path(LINUX_AT_FDCWD, pathname, host_path, sizeof(host_path), 0);
    if (r < 0) {
        pthread_mutex_unlock(&g_inotify_mutex);
        return r;
    }

    struct stat st;
    if (stat(host_path, &st) != 0) {
        pthread_mutex_unlock(&g_inotify_mutex);
        return darwin_to_linux_errno(errno);
    }

    /* Check if already watched */
    for (int i = 0; i < inst->watch_count; i++) {
        if (strcmp(inst->watches[i].path, host_path) == 0) {
            inst->watches[i].mask = mask;
            int existing_wd = inst->watches[i].wd;
            pthread_mutex_unlock(&g_inotify_mutex);
            return existing_wd;
        }
    }

    if (inst->watch_count >= MAX_INOTIFY_WATCHES) {
        pthread_mutex_unlock(&g_inotify_mutex);
        return -LINUX_ENOSPC;
    }

    int wd = inst->next_wd++;
    int idx = inst->watch_count++;
    inst->watches[idx].wd = wd;
    inst->watches[idx].mask = mask;
    strncpy(inst->watches[idx].path, host_path, sizeof(inst->watches[idx].path) - 1);
    inst->watches[idx].path[sizeof(inst->watches[idx].path) - 1] = '\0';

    pthread_mutex_unlock(&g_inotify_mutex);
    return wd;
}

int64_t handle_inotify_rm_watch(int fd, int wd) {
    if (fd < 0) return -LINUX_EBADF;
    if (wd < 1) return -LINUX_EINVAL;

    pthread_mutex_lock(&g_inotify_mutex);
    struct ltm_inotify_instance *inst = NULL;
    for (int i = 0; i < MAX_INOTIFY_INSTANCES; i++) {
        if (g_inotifys[i].active && g_inotifys[i].read_fd == fd) {
            inst = &g_inotifys[i];
            break;
        }
    }
    if (!inst) {
        pthread_mutex_unlock(&g_inotify_mutex);
        return -LINUX_EBADF;
    }

    int found = -1;
    for (int i = 0; i < inst->watch_count; i++) {
        if (inst->watches[i].wd == wd) {
            found = i;
            break;
        }
    }
    if (found == -1) {
        pthread_mutex_unlock(&g_inotify_mutex);
        return -LINUX_EINVAL;
    }

    for (int i = found; i < inst->watch_count - 1; i++) {
        inst->watches[i] = inst->watches[i + 1];
    }
    inst->watch_count--;

    pthread_mutex_unlock(&g_inotify_mutex);
    return 0;
}

/* POSIX I/O Priority */
int64_t handle_ioprio_set(int which, int who, int ioprio) {
    if (which < 1 || which > 3) return -LINUX_EINVAL;
    if (who < 0) return -LINUX_ESRCH;
    if (who > 0) {
        if (kill(who, 0) == -1 && errno == ESRCH) return -LINUX_ESRCH;
    }

    int pclass = ioprio >> 13;
    int pdata = ioprio & 0x1fff;
    if (pclass > 3 || pdata > 7) return -LINUX_EINVAL;

    return 0;
}

int64_t handle_ioprio_get(int which, int who) {
    if (which < 1 || which > 3) return -LINUX_EINVAL;
    if (who < 0) return -LINUX_ESRCH;
    if (who > 0) {
        if (kill(who, 0) == -1 && errno == ESRCH) return -LINUX_ESRCH;
    }

    /* Return default IOPRIO_CLASS_BE (2) with priority 4 */
    return (2 << 13) | 4;
}

/* POSIX Timers */
#define MAX_POSIX_TIMERS 32
struct ltm_posix_timer {
    bool active;
    int clockid;
    struct linux_timespec interval;
    struct linux_timespec value;
    int overrun;
};

static struct ltm_posix_timer g_posix_timers[MAX_POSIX_TIMERS];
static pthread_mutex_t g_timer_mutex = PTHREAD_MUTEX_INITIALIZER;

int64_t handle_timer_create(int clockid, void *sevp, int *timerid) {
    (void)sevp;
    if (clockid != 0 && clockid != 1 && clockid != 7) { /* REALTIME, MONOTONIC, BOOTTIME */
        return -LINUX_EINVAL;
    }
    if (!timerid) return -LINUX_EFAULT;

    pthread_mutex_lock(&g_timer_mutex);
    int slot = -1;
    for (int i = 0; i < MAX_POSIX_TIMERS; i++) {
        if (!g_posix_timers[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        pthread_mutex_unlock(&g_timer_mutex);
        return -LINUX_EAGAIN;
    }

    g_posix_timers[slot].active = true;
    g_posix_timers[slot].clockid = clockid;
    memset(&g_posix_timers[slot].interval, 0, sizeof(struct linux_timespec));
    memset(&g_posix_timers[slot].value, 0, sizeof(struct linux_timespec));
    g_posix_timers[slot].overrun = 0;

    *timerid = slot;
    pthread_mutex_unlock(&g_timer_mutex);
    return 0;
}

int64_t handle_timer_settime(int timerid, int flags, const struct linux_itimerspec *new_value, struct linux_itimerspec *old_value) {
    (void)flags;
    int idx = timerid;
    if (idx < 0 || idx >= MAX_POSIX_TIMERS) return -LINUX_EINVAL;
    if (!new_value) return -LINUX_EFAULT;

    if (new_value->it_value.tv_nsec < 0 || new_value->it_value.tv_nsec >= 1000000000 ||
        new_value->it_interval.tv_nsec < 0 || new_value->it_interval.tv_nsec >= 1000000000) {
        return -LINUX_EINVAL;
    }

    pthread_mutex_lock(&g_timer_mutex);
    if (!g_posix_timers[idx].active) {
        pthread_mutex_unlock(&g_timer_mutex);
        return -LINUX_EINVAL;
    }

    if (old_value) {
        old_value->it_interval = g_posix_timers[idx].interval;
        old_value->it_value = g_posix_timers[idx].value;
    }

    g_posix_timers[idx].interval = new_value->it_interval;
    g_posix_timers[idx].value = new_value->it_value;

    pthread_mutex_unlock(&g_timer_mutex);
    return 0;
}

int64_t handle_timer_gettime(int timerid, struct linux_itimerspec *curr_value) {
    int idx = timerid;
    if (idx < 0 || idx >= MAX_POSIX_TIMERS) return -LINUX_EINVAL;
    if (!curr_value) return -LINUX_EFAULT;

    pthread_mutex_lock(&g_timer_mutex);
    if (!g_posix_timers[idx].active) {
        pthread_mutex_unlock(&g_timer_mutex);
        return -LINUX_EINVAL;
    }

    curr_value->it_interval = g_posix_timers[idx].interval;
    curr_value->it_value = g_posix_timers[idx].value;

    pthread_mutex_unlock(&g_timer_mutex);
    return 0;
}

int64_t handle_timer_getoverrun(int timerid) {
    int idx = timerid;
    if (idx < 0 || idx >= MAX_POSIX_TIMERS) return -LINUX_EINVAL;

    pthread_mutex_lock(&g_timer_mutex);
    if (!g_posix_timers[idx].active) {
        pthread_mutex_unlock(&g_timer_mutex);
        return -LINUX_EINVAL;
    }

    int ov = g_posix_timers[idx].overrun;
    pthread_mutex_unlock(&g_timer_mutex);
    return ov;
}

int64_t handle_timer_delete(int timerid) {
    int idx = timerid;
    if (idx < 0 || idx >= MAX_POSIX_TIMERS) return -LINUX_EINVAL;

    pthread_mutex_lock(&g_timer_mutex);
    if (!g_posix_timers[idx].active) {
        pthread_mutex_unlock(&g_timer_mutex);
        return -LINUX_EINVAL;
    }

    g_posix_timers[idx].active = false;
    pthread_mutex_unlock(&g_timer_mutex);
    return 0;
}

/* Real-Time Signals */
int64_t handle_rt_sigsuspend(const uint64_t *unewset, size_t sigsetsize) {
    if (sigsetsize != 8) return -LINUX_EINVAL;
    if (!unewset) return -LINUX_EFAULT;

    sigset_t dset;
    sigemptyset(&dset);
    uint64_t mask = *unewset;
    for (int s = 1; s <= 64; s++) {
        if (mask & (1ULL << (s - 1))) {
            int d_sig = linux_to_darwin_signal(s);
            if (d_sig > 0 && d_sig != SIGKILL && d_sig != SIGSTOP) {
                sigaddset(&dset, d_sig);
            }
        }
    }
    sigsuspend(&dset);
    return -LINUX_EINTR;
}

int64_t handle_rt_sigtimedwait(const uint64_t *uthese, struct linux_siginfo *uinfo, const struct linux_timespec *uts, size_t sigsetsize) {
    if (sigsetsize != 8) return -LINUX_EINVAL;
    if (!uthese) return -LINUX_EFAULT;
    if (uts) {
        if (uts->tv_nsec < 0 || uts->tv_nsec >= 1000000000 || uts->tv_sec < 0) {
            return -LINUX_EINVAL;
        }
    }

    sigset_t dset;
    sigemptyset(&dset);
    uint64_t mask = *uthese;
    for (int s = 1; s <= 64; s++) {
        if (mask & (1ULL << (s - 1))) {
            int d_sig = linux_to_darwin_signal(s);
            if (d_sig > 0 && d_sig != SIGKILL && d_sig != SIGSTOP) {
                sigaddset(&dset, d_sig);
            }
        }
    }

    sigset_t pending;
    sigemptyset(&pending);
    sigpending(&pending);

    for (int s = 1; s <= 31; s++) {
        if (sigismember(&pending, s) && sigismember(&dset, s)) {
            int l_sig = darwin_to_linux_signal(s);
            if (uinfo) {
                memset(uinfo, 0, sizeof(*uinfo));
                uinfo->si_signo = l_sig;
                uinfo->si_code = 0; /* SI_USER */
            }
            return l_sig;
        }
    }

    return -LINUX_EAGAIN;
}

int64_t handle_rt_sigqueueinfo(pid_t tgid, int sig, struct linux_siginfo *uinfo) {
    if (sig < 0 || sig >= LINUX_NSIG) return -LINUX_EINVAL;
    if (!uinfo) return -LINUX_EFAULT;
    if (tgid <= 0) {
        if (tgid < 0) return -LINUX_EINVAL;
    }
    if (tgid > 0) {
        if (kill(tgid, 0) == -1 && errno == ESRCH) return -LINUX_ESRCH;
    }

    if (sig == 0) return 0;
    if (kill(tgid, sig) != 0) return darwin_to_linux_errno(errno);
    return 0;
}

/* System V IPC */
int64_t handle_msgget(key_t key, int msgflg) {
    int r = msgget(key, msgflg);
    if (r == -1) return darwin_to_linux_errno(errno);
    return r;
}

int64_t handle_msgctl(int msqid, int cmd, void *buf) {
    int dcmd = cmd;
    if (cmd == 0) dcmd = IPC_RMID;
    else if (cmd == 1) dcmd = IPC_SET;
    else if (cmd == 2) dcmd = IPC_STAT;

    if (dcmd == IPC_STAT) {
        if (!buf) return -LINUX_EFAULT;
        struct msqid_ds ds;
        int r = msgctl(msqid, IPC_STAT, &ds);
        if (r == -1) return darwin_to_linux_errno(errno);

        struct linux_msqid_ds *l = (struct linux_msqid_ds *)buf;
        memset(l, 0, sizeof(*l));
        l->msg_perm.uid = ds.msg_perm.uid;
        l->msg_perm.gid = ds.msg_perm.gid;
        l->msg_perm.cuid = ds.msg_perm.cuid;
        l->msg_perm.cgid = ds.msg_perm.cgid;
        l->msg_perm.mode = ds.msg_perm.mode;
        l->msg_stime = ds.msg_stime;
        l->msg_rtime = ds.msg_rtime;
        l->msg_ctime = ds.msg_ctime;
        l->msg_cbytes = ds.msg_cbytes;
        l->msg_qnum = ds.msg_qnum;
        l->msg_qbytes = ds.msg_qbytes;
        l->msg_lspid = ds.msg_lspid;
        l->msg_lrpid = ds.msg_lrpid;
        return 0;
    } else if (dcmd == IPC_SET) {
        if (!buf) return -LINUX_EFAULT;
        struct linux_msqid_ds *l = (struct linux_msqid_ds *)buf;
        struct msqid_ds ds;
        memset(&ds, 0, sizeof(ds));
        ds.msg_perm.uid = l->msg_perm.uid;
        ds.msg_perm.gid = l->msg_perm.gid;
        ds.msg_perm.mode = l->msg_perm.mode;
        ds.msg_qbytes = l->msg_qbytes;
        int r = msgctl(msqid, IPC_SET, &ds);
        if (r == -1) return darwin_to_linux_errno(errno);
        return 0;
    }

    int r = msgctl(msqid, dcmd, NULL);
    if (r == -1) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg) {
    if (!msgp) return -LINUX_EFAULT;
    ssize_t r = msgrcv(msqid, msgp, msgsz, msgtyp, msgflg);
    if (r == -1) return darwin_to_linux_errno(errno);
    return r;
}

int64_t handle_msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg) {
    if (!msgp) return -LINUX_EFAULT;
    int r = msgsnd(msqid, msgp, msgsz, msgflg);
    if (r == -1) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_semget(key_t key, int nsems, int semflg) {
    if (nsems < 0) return -LINUX_EINVAL;
    int r = semget(key, nsems, semflg);
    if (r == -1) return darwin_to_linux_errno(errno);
    return r;
}

int64_t handle_semctl(int semid, int semnum, int cmd, unsigned long arg) {
    int dcmd = cmd;
    switch (cmd) {
        case 0: dcmd = IPC_RMID; break;
        case 1: dcmd = IPC_SET; break;
        case 2: dcmd = IPC_STAT; break;
        case 11: dcmd = GETPID; break;
        case 12: dcmd = GETVAL; break;
        case 13: dcmd = GETALL; break;
        case 14: dcmd = GETNCNT; break;
        case 15: dcmd = GETZCNT; break;
        case 16: dcmd = SETVAL; break;
        case 17: dcmd = SETALL; break;
        default: break;
    }

    if (dcmd == IPC_STAT) {
        void *buf = (void *)arg;
        if (!buf) return -LINUX_EFAULT;
        struct semid_ds ds;
        int r = semctl(semid, semnum, IPC_STAT, &ds);
        if (r == -1) return darwin_to_linux_errno(errno);

        struct linux_semid_ds *l = (struct linux_semid_ds *)buf;
        memset(l, 0, sizeof(*l));
        l->sem_perm.uid = ds.sem_perm.uid;
        l->sem_perm.gid = ds.sem_perm.gid;
        l->sem_perm.cuid = ds.sem_perm.cuid;
        l->sem_perm.cgid = ds.sem_perm.cgid;
        l->sem_perm.mode = ds.sem_perm.mode;
        l->sem_otime = ds.sem_otime;
        l->sem_ctime = ds.sem_ctime;
        l->sem_nsems = ds.sem_nsems;
        return 0;
    } else if (dcmd == IPC_SET) {
        void *buf = (void *)arg;
        if (!buf) return -LINUX_EFAULT;
        struct linux_semid_ds *l = (struct linux_semid_ds *)buf;
        struct semid_ds ds;
        memset(&ds, 0, sizeof(ds));
        ds.sem_perm.uid = l->sem_perm.uid;
        ds.sem_perm.gid = l->sem_perm.gid;
        ds.sem_perm.mode = l->sem_perm.mode;
        int r = semctl(semid, semnum, IPC_SET, &ds);
        if (r == -1) return darwin_to_linux_errno(errno);
        return 0;
    }

    int r = semctl(semid, semnum, dcmd, (void *)arg);
    if (r == -1) return darwin_to_linux_errno(errno);
    return r;
}

int64_t handle_semop(int semid, void *sops, size_t nsops) {
    if (!sops) return -LINUX_EFAULT;
    if (nsops == 0) return -LINUX_EINVAL;
    int r = semop(semid, (struct sembuf *)sops, nsops);
    if (r == -1) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_semtimedop(int semid, void *sops, size_t nsops, const struct linux_timespec *timeout) {
    if (!sops) return -LINUX_EFAULT;
    if (nsops == 0) return -LINUX_EINVAL;
    if (timeout) {
        if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L) {
            return -LINUX_EINVAL;
        }
    }
    int r = semop(semid, (struct sembuf *)sops, nsops);
    if (r == -1) return darwin_to_linux_errno(errno);
    return 0;
}

/* Vector I/O & Filesystem */
int64_t handle_preadv2(int fd, const struct linux_iovec *iov, int iovcnt, off_t offset, int flags) {
    if (flags & ~(0x01 | 0x02 | 0x04 | 0x08 | 0x10)) return -LINUX_EINVAL;
    if (offset < -1) return -LINUX_EINVAL;
    if (iovcnt < 0 || iovcnt > 1024) return -LINUX_EINVAL;
    if (iovcnt > 0 && !iov) return -LINUX_EFAULT;
    if (fd < 0) return -LINUX_EBADF;

    if (offset == -1) {
        return handle_readv(fd, iov, iovcnt);
    }

    ssize_t total = 0;
    off_t cur_off = offset;
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base && iov[i].iov_len > 0) return -LINUX_EFAULT;
        if (iov[i].iov_len == 0) continue;
        ssize_t n = pread(fd, (void *)iov[i].iov_base, iov[i].iov_len, cur_off);
        if (n < 0) {
            if (total > 0) return total;
            return darwin_to_linux_errno(errno);
        }
        total += n;
        cur_off += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

int64_t handle_pwritev2(int fd, const struct linux_iovec *iov, int iovcnt, off_t offset, int flags) {
    if (flags & ~(0x01 | 0x02 | 0x04 | 0x08 | 0x10)) return -LINUX_EINVAL;
    if (offset < -1) return -LINUX_EINVAL;
    if (iovcnt < 0 || iovcnt > 1024) return -LINUX_EINVAL;
    if (iovcnt > 0 && !iov) return -LINUX_EFAULT;
    if (fd < 0) return -LINUX_EBADF;

    if (offset == -1) {
        return handle_writev(fd, iov, iovcnt);
    }

    ssize_t total = 0;
    off_t cur_off = offset;
    for (int i = 0; i < iovcnt; i++) {
        if (!iov[i].iov_base && iov[i].iov_len > 0) return -LINUX_EFAULT;
        if (iov[i].iov_len == 0) continue;
        ssize_t n = pwrite(fd, (const void *)iov[i].iov_base, iov[i].iov_len, cur_off);
        if (n < 0) {
            if (total > 0) return total;
            return darwin_to_linux_errno(errno);
        }
        total += n;
        cur_off += n;
        if ((size_t)n < iov[i].iov_len) break;
    }
    return total;
}

int64_t handle_statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct linux_statx *statxbuf) {
    if (flags & ~(0x100 | 0x800 | 0x1000 | 0x6000)) return -LINUX_EINVAL;
    if (!statxbuf) return -LINUX_EFAULT;
    if (!pathname) return -LINUX_EFAULT;

    struct stat st;
    if (pathname[0] == '\0') {
        if (!(flags & 0x1000)) return -LINUX_ENOENT; /* AT_EMPTY_PATH */
        if (dirfd < 0) return -LINUX_EBADF;
        if (fstat(dirfd, &st) != 0) return darwin_to_linux_errno(errno);
    } else {
        char host_path[PATH_MAX];
        int r = sandbox_resolve_path(dirfd, pathname, host_path, sizeof(host_path), (flags & 0x100) ? AT_SYMLINK_NOFOLLOW : 0);
        if (r < 0) return r;

        if (r == 1) {
            int vres = sandbox_virtual_stat(host_path, &st);
            if (vres < 0) return vres;
        } else if (flags & 0x100) { /* AT_SYMLINK_NOFOLLOW */
            if (lstat(host_path, &st) != 0) return darwin_to_linux_errno(errno);
        } else {
            if (stat(host_path, &st) != 0) return darwin_to_linux_errno(errno);
        }
    }

    memset(statxbuf, 0, sizeof(*statxbuf));
    statxbuf->stx_mask = mask & 0x0fff;
    statxbuf->stx_blksize = st.st_blksize;
    statxbuf->stx_nlink = st.st_nlink;
    statxbuf->stx_uid = 0; /* root container */
    statxbuf->stx_gid = 0;
    statxbuf->stx_mode = st.st_mode;
    statxbuf->stx_ino = st.st_ino;
    statxbuf->stx_size = st.st_size;
    statxbuf->stx_blocks = st.st_blocks;

    statxbuf->stx_atime.tv_sec = st.st_atime;
    statxbuf->stx_atime.tv_nsec = st.st_atimespec.tv_nsec;
    statxbuf->stx_btime.tv_sec = st.st_birthtime;
    statxbuf->stx_btime.tv_nsec = st.st_birthtimespec.tv_nsec;
    statxbuf->stx_ctime.tv_sec = st.st_ctime;
    statxbuf->stx_ctime.tv_nsec = st.st_ctimespec.tv_nsec;
    statxbuf->stx_mtime.tv_sec = st.st_mtime;
    statxbuf->stx_mtime.tv_nsec = st.st_mtimespec.tv_nsec;

    statxbuf->stx_dev_major = major(st.st_dev);
    statxbuf->stx_dev_minor = minor(st.st_dev);
    if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode)) {
        statxbuf->stx_rdev_major = major(st.st_rdev);
        statxbuf->stx_rdev_minor = minor(st.st_rdev);
    }

    return 0;
}

int64_t handle_execveat(int dirfd, const char *pathname, char *const argv[], char *const envp[], int flags) {
    (void)dirfd;
    if (!pathname) return -LINUX_EFAULT;
    if (flags & ~(0x100 | 0x1000)) return -LINUX_EINVAL;
    return handle_execve(pathname, argv, envp);
}

/* ========================================================================= */
/* Wave 6: System V Shared Memory Subsystem                                  */
/* ========================================================================= */

int64_t handle_shmget(key_t key, size_t size, int shmflg) {
    if (size == 0 && (shmflg & 01000 /* IPC_CREAT */)) {
        return -LINUX_EINVAL;
    }
    int id = shmget(key, size, shmflg);
    if (id < 0) return darwin_to_linux_errno(errno);
    return id;
}

int64_t handle_shmctl(int shmid, int cmd, void *buf) {
    if (shmid < 0) return -LINUX_EINVAL;

    if (cmd == 0 /* IPC_RMID */) {
        if (shmctl(shmid, IPC_RMID, NULL) != 0) {
            return darwin_to_linux_errno(errno);
        }
        return 0;
    } else if (cmd == 2 /* IPC_STAT */) {
        if (!buf) return -LINUX_EFAULT;
        struct shmid_ds ds;
        if (shmctl(shmid, IPC_STAT, &ds) != 0) {
            return darwin_to_linux_errno(errno);
        }
        struct linux_shmid_ds *lbuf = (struct linux_shmid_ds *)buf;
        memset(lbuf, 0, sizeof(*lbuf));
        lbuf->shm_perm.key = ds.shm_perm._key;
        lbuf->shm_perm.uid = 0; /* root container */
        lbuf->shm_perm.gid = 0;
        lbuf->shm_perm.cuid = 0;
        lbuf->shm_perm.cgid = 0;
        lbuf->shm_perm.mode = ds.shm_perm.mode;
        lbuf->shm_perm.seq = ds.shm_perm._seq;
        lbuf->shm_segsz = ds.shm_segsz;
        lbuf->shm_atime = ds.shm_atime;
        lbuf->shm_dtime = ds.shm_dtime;
        lbuf->shm_ctime = ds.shm_ctime;
        lbuf->shm_cpid = ds.shm_cpid;
        lbuf->shm_lpid = ds.shm_lpid;
        lbuf->shm_nattch = ds.shm_nattch;
        return 0;
    } else if (cmd == 1 /* IPC_SET */) {
        if (!buf) return -LINUX_EFAULT;
        struct shmid_ds ds;
        if (shmctl(shmid, IPC_STAT, &ds) != 0) {
            return darwin_to_linux_errno(errno);
        }
        struct linux_shmid_ds *lbuf = (struct linux_shmid_ds *)buf;
        ds.shm_perm.mode = lbuf->shm_perm.mode & 0777;
        if (shmctl(shmid, IPC_SET, &ds) != 0) {
            return darwin_to_linux_errno(errno);
        }
        return 0;
    } else if (cmd == 3 /* IPC_INFO */ || cmd == 14 /* SHM_INFO */) {
        return 0;
    }
    return -LINUX_EINVAL;
}

int64_t handle_shmat(int shmid, const void *shmaddr, int shmflg) {
    if (shmid < 0) return -LINUX_EINVAL;
    if (shmaddr != NULL) {
        if (!(shmflg & 020000 /* SHM_RND */) && ((uintptr_t)shmaddr % PAGE_SIZE_16K != 0)) {
            return -LINUX_EINVAL;
        }
        if (shmflg & 020000 /* SHM_RND */) {
            shmaddr = (const void *)PAGE_ROUND_DOWN(shmaddr);
        }
    }
    void *addr = shmat(shmid, shmaddr, shmflg);
    if (addr == (void *)-1) {
        return darwin_to_linux_errno(errno);
    }
    return (int64_t)(uintptr_t)addr;
}

int64_t handle_shmdt(const void *shmaddr) {
    if (!shmaddr || ((uintptr_t)shmaddr % PAGE_SIZE_16K != 0)) {
        return -LINUX_EINVAL;
    }
    if (shmdt(shmaddr) != 0) {
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

/* ========================================================================= */
/* Wave 6: POSIX Message Queues Emulation                                    */
/* ========================================================================= */

#define MAX_LTM_MQUEUES 64
#define MAX_MQ_MESSAGES 32
#define MAX_MQ_BUFFER 8192

typedef struct {
    char data[MAX_MQ_BUFFER];
    size_t len;
    unsigned int prio;
} ltm_mq_item_t;

typedef struct {
    bool active;
    char name[256];
    long mq_flags;
    long mq_maxmsg;
    long mq_msgsize;
    long mq_curmsgs;
    ltm_mq_item_t msgs[MAX_MQ_MESSAGES];
    pthread_mutex_t lock;
    pthread_cond_t cond_read;
    pthread_cond_t cond_write;
    int refcount;
} ltm_mqueue_obj_t;

typedef struct {
    bool open;
    int mq_idx;
    int oflag;
} ltm_mq_fd_t;

#define MAX_MQ_DESCS 256
static ltm_mqueue_obj_t g_mqueues[MAX_LTM_MQUEUES];
static ltm_mq_fd_t g_mq_descs[MAX_MQ_DESCS];
static pthread_mutex_t g_mq_global_lock = PTHREAD_MUTEX_INITIALIZER;

void ltm_mq_close(int fd) {
    int d = fd - 1000;
    if (d >= 0 && d < MAX_MQ_DESCS) {
        pthread_mutex_lock(&g_mq_global_lock);
        if (g_mq_descs[d].open) {
            int idx = g_mq_descs[d].mq_idx;
            g_mq_descs[d].open = false;
            if (idx >= 0 && idx < MAX_LTM_MQUEUES && g_mqueues[idx].active) {
                g_mqueues[idx].refcount--;
                if (g_mqueues[idx].refcount <= 0 && g_mqueues[idx].name[0] == '\0') {
                    g_mqueues[idx].active = false;
                }
            }
        }
        pthread_mutex_unlock(&g_mq_global_lock);
    }
}

int64_t handle_mq_open(const char *name, int oflag, mode_t mode, struct linux_mq_attr *attr) {
    (void)mode;
    if (!name) return -LINUX_EFAULT;
    if (name[0] != '/') return -LINUX_EINVAL;
    if (strchr(name + 1, '/') != NULL) return -LINUX_EACCES;
    if (strlen(name) > 255) return -LINUX_ENAMETOOLONG;

    pthread_mutex_lock(&g_mq_global_lock);
    int found_idx = -1;
    for (int i = 0; i < MAX_LTM_MQUEUES; i++) {
        if (g_mqueues[i].active && strcmp(g_mqueues[i].name, name) == 0) {
            found_idx = i;
            break;
        }
    }

    if (found_idx >= 0) {
        if ((oflag & (LINUX_O_CREAT | LINUX_O_EXCL)) == (LINUX_O_CREAT | LINUX_O_EXCL)) {
            pthread_mutex_unlock(&g_mq_global_lock);
            return -LINUX_EEXIST;
        }
    } else {
        if (!(oflag & LINUX_O_CREAT)) {
            pthread_mutex_unlock(&g_mq_global_lock);
            return -LINUX_ENOENT;
        }
        for (int i = 0; i < MAX_LTM_MQUEUES; i++) {
            if (!g_mqueues[i].active) {
                found_idx = i;
                break;
            }
        }
        if (found_idx < 0) {
            pthread_mutex_unlock(&g_mq_global_lock);
            return -LINUX_ENOSPC;
        }
        ltm_mqueue_obj_t *mq = &g_mqueues[found_idx];
        memset(mq, 0, sizeof(*mq));
        mq->active = true;
        strncpy(mq->name, name, sizeof(mq->name) - 1);
        mq->mq_flags = (oflag & LINUX_O_NONBLOCK) ? LINUX_O_NONBLOCK : 0;
        if (attr) {
            mq->mq_maxmsg = (attr->mq_maxmsg > 0 && attr->mq_maxmsg <= MAX_MQ_MESSAGES) ? attr->mq_maxmsg : 10;
            mq->mq_msgsize = (attr->mq_msgsize > 0 && attr->mq_msgsize <= MAX_MQ_BUFFER) ? attr->mq_msgsize : MAX_MQ_BUFFER;
        } else {
            mq->mq_maxmsg = 10;
            mq->mq_msgsize = MAX_MQ_BUFFER;
        }
        mq->mq_curmsgs = 0;
        pthread_mutex_init(&mq->lock, NULL);
        pthread_cond_init(&mq->cond_read, NULL);
        pthread_cond_init(&mq->cond_write, NULL);
        mq->refcount = 0;
    }

    int free_desc = -1;
    for (int d = 0; d < MAX_MQ_DESCS; d++) {
        if (!g_mq_descs[d].open) {
            free_desc = d;
            break;
        }
    }
    if (free_desc < 0) {
        pthread_mutex_unlock(&g_mq_global_lock);
        return -LINUX_EMFILE;
    }

    g_mq_descs[free_desc].open = true;
    g_mq_descs[free_desc].mq_idx = found_idx;
    g_mq_descs[free_desc].oflag = oflag;
    g_mqueues[found_idx].refcount++;

    pthread_mutex_unlock(&g_mq_global_lock);
    return free_desc + 1000;
}

int64_t handle_mq_unlink(const char *name) {
    if (!name) return -LINUX_EFAULT;
    if (name[0] != '/') return -LINUX_EINVAL;

    pthread_mutex_lock(&g_mq_global_lock);
    int found = -1;
    for (int i = 0; i < MAX_LTM_MQUEUES; i++) {
        if (g_mqueues[i].active && strcmp(g_mqueues[i].name, name) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        pthread_mutex_unlock(&g_mq_global_lock);
        return -LINUX_ENOENT;
    }

    g_mqueues[found].name[0] = '\0';
    if (g_mqueues[found].refcount <= 0) {
        g_mqueues[found].active = false;
    }
    pthread_mutex_unlock(&g_mq_global_lock);
    return 0;
}

int64_t handle_mq_timedsend(int mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio, const struct linux_timespec *abs_timeout) {
    (void)abs_timeout;
    int d = mqdes - 1000;
    if (d < 0 || d >= MAX_MQ_DESCS || !g_mq_descs[d].open) return -LINUX_EBADF;
    if (!msg_ptr) return -LINUX_EFAULT;

    int idx = g_mq_descs[d].mq_idx;
    ltm_mqueue_obj_t *mq = &g_mqueues[idx];
    if (msg_len > (size_t)mq->mq_msgsize) return -LINUX_EMSGSIZE;

    pthread_mutex_lock(&mq->lock);
    if (mq->mq_curmsgs >= mq->mq_maxmsg) {
        if ((g_mq_descs[d].oflag & LINUX_O_NONBLOCK) || (mq->mq_flags & LINUX_O_NONBLOCK)) {
            pthread_mutex_unlock(&mq->lock);
            return -LINUX_EAGAIN;
        }
    }

    /* Insert ordered by prio descending */
    int pos = (int)mq->mq_curmsgs;
    for (int i = 0; i < (int)mq->mq_curmsgs; i++) {
        if (msg_prio > mq->msgs[i].prio) {
            pos = i;
            break;
        }
    }
    for (int i = (int)mq->mq_curmsgs; i > pos; i--) {
        mq->msgs[i] = mq->msgs[i - 1];
    }
    memcpy(mq->msgs[pos].data, msg_ptr, msg_len);
    mq->msgs[pos].len = msg_len;
    mq->msgs[pos].prio = msg_prio;
    mq->mq_curmsgs++;

    pthread_cond_signal(&mq->cond_read);
    pthread_mutex_unlock(&mq->lock);
    return 0;
}

int64_t handle_mq_timedreceive(int mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio, const struct linux_timespec *abs_timeout) {
    (void)abs_timeout;
    int d = mqdes - 1000;
    if (d < 0 || d >= MAX_MQ_DESCS || !g_mq_descs[d].open) return -LINUX_EBADF;
    if (!msg_ptr) return -LINUX_EFAULT;

    int idx = g_mq_descs[d].mq_idx;
    ltm_mqueue_obj_t *mq = &g_mqueues[idx];
    if (msg_len < (size_t)mq->mq_msgsize) return -LINUX_EMSGSIZE;

    pthread_mutex_lock(&mq->lock);
    if (mq->mq_curmsgs == 0) {
        if ((g_mq_descs[d].oflag & LINUX_O_NONBLOCK) || (mq->mq_flags & LINUX_O_NONBLOCK)) {
            pthread_mutex_unlock(&mq->lock);
            return -LINUX_EAGAIN;
        }
    }
    if (mq->mq_curmsgs == 0) {
        pthread_mutex_unlock(&mq->lock);
        return -LINUX_EAGAIN;
    }

    size_t copy_len = mq->msgs[0].len;
    memcpy(msg_ptr, mq->msgs[0].data, copy_len);
    if (msg_prio) *msg_prio = mq->msgs[0].prio;

    for (int i = 0; i < (int)mq->mq_curmsgs - 1; i++) {
        mq->msgs[i] = mq->msgs[i + 1];
    }
    mq->mq_curmsgs--;

    pthread_cond_signal(&mq->cond_write);
    pthread_mutex_unlock(&mq->lock);
    return (int64_t)copy_len;
}

int64_t handle_mq_notify(int mqdes, const void *notification) {
    (void)notification;
    int d = mqdes - 1000;
    if (d < 0 || d >= MAX_MQ_DESCS || !g_mq_descs[d].open) return -LINUX_EBADF;
    return 0;
}

int64_t handle_mq_getsetattr(int mqdes, const struct linux_mq_attr *newattr, struct linux_mq_attr *oldattr) {
    int d = mqdes - 1000;
    if (d < 0 || d >= MAX_MQ_DESCS || !g_mq_descs[d].open) return -LINUX_EBADF;

    int idx = g_mq_descs[d].mq_idx;
    ltm_mqueue_obj_t *mq = &g_mqueues[idx];
    if (oldattr) {
        oldattr->mq_flags = (g_mq_descs[d].oflag & LINUX_O_NONBLOCK) ? LINUX_O_NONBLOCK : 0;
        oldattr->mq_maxmsg = mq->mq_maxmsg;
        oldattr->mq_msgsize = mq->mq_msgsize;
        oldattr->mq_curmsgs = mq->mq_curmsgs;
    }
    if (newattr) {
        if (newattr->mq_flags & LINUX_O_NONBLOCK) {
            g_mq_descs[d].oflag |= LINUX_O_NONBLOCK;
            mq->mq_flags |= LINUX_O_NONBLOCK;
        } else {
            g_mq_descs[d].oflag &= ~LINUX_O_NONBLOCK;
            mq->mq_flags &= ~LINUX_O_NONBLOCK;
        }
    }
    return 0;
}

/* ========================================================================= */
/* Wave 6: Multi-Message Socket I/O (sendmmsg / recvmmsg)                    */
/* ========================================================================= */

int64_t handle_sendmmsg(int sockfd, struct linux_mmsghdr *msgvec, unsigned int vlen, unsigned int flags) {
    if (vlen > 0 && !msgvec) return -LINUX_EFAULT;
    if (vlen > 1024) return -LINUX_EINVAL;
    if (vlen == 0) return 0;

    unsigned int sent = 0;
    for (unsigned int i = 0; i < vlen; i++) {
        int64_t ret = handle_sendmsg(sockfd, &msgvec[i].msg_hdr, flags);
        if (ret < 0) {
            if (sent > 0) return sent;
            return ret;
        }
        msgvec[i].msg_len = (unsigned int)ret;
        sent++;
    }
    return sent;
}

int64_t handle_recvmmsg(int sockfd, struct linux_mmsghdr *msgvec, unsigned int vlen, unsigned int flags, struct linux_timespec *timeout) {
    (void)timeout;
    if (vlen > 0 && !msgvec) return -LINUX_EFAULT;
    if (vlen > 1024) return -LINUX_EINVAL;
    if (vlen == 0) return 0;

    unsigned int recvd = 0;
    for (unsigned int i = 0; i < vlen; i++) {
        int64_t ret = handle_recvmsg(sockfd, &msgvec[i].msg_hdr, flags);
        if (ret < 0) {
            if (recvd > 0) return recvd;
            return ret;
        }
        msgvec[i].msg_len = (unsigned int)ret;
        recvd++;
    }
    return recvd;
}

/* ========================================================================= */
/* Wave 6: Time Adjustment & Setter Syscalls                                 */
/* ========================================================================= */

int64_t handle_clock_settime(clockid_t clockid, const struct linux_timespec *tp) {
    if (!tp) return -LINUX_EFAULT;
    if (tp->tv_nsec < 0 || tp->tv_nsec >= 1000000000L) return -LINUX_EINVAL;
    if (clockid != 0 /* CLOCK_REALTIME */) return -LINUX_EINVAL;
    return 0; /* Virtualized container root success */
}

int64_t handle_settimeofday(const struct timeval *tv, const struct timezone *tz) {
    (void)tz;
    if (!tv && !tz) return 0;
    if (tv) {
        if (tv->tv_usec < 0 || tv->tv_usec >= 1000000) return -LINUX_EINVAL;
    }
    return 0; /* Virtualized container root success */
}

int64_t handle_adjtimex(struct linux_timex *buf) {
    if (!buf) return -LINUX_EFAULT;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    memset(buf, 0, sizeof(*buf));
    buf->time.tv_sec = tv.tv_sec;
    buf->time.tv_usec = tv.tv_usec;
    buf->tick = 10000;
    buf->maxerror = 500000;
    buf->esterror = 500000;
    buf->status = 0;
    return 0; /* TIME_OK */
}

int64_t handle_clock_adjtime(clockid_t clk_id, struct linux_timex *buf) {
    if (clk_id != 0 /* CLOCK_REALTIME */) return -LINUX_EINVAL;
    if (!buf) return -LINUX_EFAULT;
    return handle_adjtimex(buf);
}

/* ========================================================================= */
/* Wave 6: Advanced Memory & Synchronization Syscalls                        */
/* ========================================================================= */

int64_t handle_mlock2(unsigned long addr, size_t len, int flags) {
    if (flags & ~1) return -LINUX_EINVAL; /* MLOCK_ONFAULT is 1 */
    return handle_mlock(addr, len);
}

int64_t handle_membarrier(int cmd, unsigned int flags, int cpu_id) {
    (void)cpu_id;
    if (flags != 0) return -LINUX_EINVAL;
    switch (cmd) {
        case 0: /* MEMBARRIER_CMD_QUERY */
            return (1 << 0) | (1 << 1) | (1 << 3); /* GLOBAL | GLOBAL_EXPEDITED | PRIVATE_EXPEDITED */
        case 1: /* MEMBARRIER_CMD_GLOBAL */
        case 2: /* MEMBARRIER_CMD_GLOBAL_EXPEDITED */
        case 8: /* MEMBARRIER_CMD_PRIVATE_EXPEDITED */
            __sync_synchronize();
            return 0;
        case 4:  /* MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED */
        case 16: /* MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED */
            return 0;
        default:
            return -LINUX_EINVAL;
    }
}

int64_t handle_userfaultfd(int flags) {
    if (flags & ~(0x080000 | 0x000800)) return -LINUX_EINVAL; /* UFFD_CLOEXEC | UFFD_NONBLOCK */
    int p[2];
    if (pipe(p) != 0) return darwin_to_linux_errno(errno);
    if (flags & 0x000800) {
        fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
    }
    if (flags & 0x080000) {
        fcntl(p[0], F_SETFD, FD_CLOEXEC);
        fcntl(p[1], F_SETFD, FD_CLOEXEC);
    }
    return p[0];
}

int64_t handle_rseq(void *rseq, uint32_t rseq_len, int flags, uint32_t sig) {
    (void)sig;
    if (!rseq) return -LINUX_EFAULT;
    if (flags & ~1) return -LINUX_EINVAL; /* RSEQ_FLAG_UNREGISTER = 1 */
    if (rseq_len != 32) return -LINUX_EINVAL;
    return 0;
}

int64_t handle_sched_setattr(pid_t pid, void *attr, unsigned int flags) {
    (void)pid; (void)flags;
    if (!attr) return -LINUX_EFAULT;
    return 0;
}

int64_t handle_sched_getattr(pid_t pid, void *attr, unsigned int size, unsigned int flags) {
    (void)pid; (void)flags;
    if (!attr) return -LINUX_EFAULT;
    if (size < 48) return -LINUX_EINVAL;
    memset(attr, 0, size);
    *(uint32_t *)attr = 48;
    return 0;
}

int64_t handle_rt_tgsigqueueinfo(pid_t tgid, pid_t tid, int sig, struct linux_siginfo *uinfo) {
    (void)tid;
    if (!uinfo) return -LINUX_EFAULT;
    if (sig < 0 || sig > 64) return -LINUX_EINVAL;
    if (tgid <= 0 || tid <= 0) return -LINUX_EINVAL;
    return handle_kill(tgid, sig);
}

/* ========================================================================= */
/* Wave 6: Process File Descriptors (pidfd_open, pidfd_send_signal, pidfd_getfd) */
/* ========================================================================= */

#define MAX_PIDFDS 256
static struct {
    int fd;
    pid_t pid;
    bool active;
} g_pidfds[MAX_PIDFDS];
static pthread_mutex_t g_pidfd_lock = PTHREAD_MUTEX_INITIALIZER;

void ltm_pidfd_close(int fd) {
    pthread_mutex_lock(&g_pidfd_lock);
    for (int i = 0; i < MAX_PIDFDS; i++) {
        if (g_pidfds[i].active && g_pidfds[i].fd == fd) {
            g_pidfds[i].active = false;
            break;
        }
    }
    pthread_mutex_unlock(&g_pidfd_lock);
}

int64_t handle_pidfd_open(pid_t pid, unsigned int flags) {
    if (flags & ~0x800) return -LINUX_EINVAL; /* PIDFD_NONBLOCK */
    if (pid <= 0) return -LINUX_EINVAL;

    if (kill(pid, 0) == -1 && errno == ESRCH) {
        return -LINUX_ESRCH;
    }

    int p[2];
    if (pipe(p) != 0) return darwin_to_linux_errno(errno);

    if (flags & 0x800) {
        fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
    }
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    fcntl(p[1], F_SETFD, FD_CLOEXEC);

    pthread_mutex_lock(&g_pidfd_lock);
    for (int i = 0; i < MAX_PIDFDS; i++) {
        if (!g_pidfds[i].active) {
            g_pidfds[i].fd = p[0];
            g_pidfds[i].pid = pid;
            g_pidfds[i].active = true;
            break;
        }
    }
    pthread_mutex_unlock(&g_pidfd_lock);

    return p[0];
}

int64_t handle_pidfd_send_signal(int pidfd, int sig, struct linux_siginfo *info, unsigned int flags) {
    (void)info;
    if (flags != 0) return -LINUX_EINVAL;
    if (pidfd < 0) return -LINUX_EBADF;
    if (sig < 0 || sig > 64) return -LINUX_EINVAL;

    pid_t target_pid = -1;
    pthread_mutex_lock(&g_pidfd_lock);
    for (int i = 0; i < MAX_PIDFDS; i++) {
        if (g_pidfds[i].active && g_pidfds[i].fd == pidfd) {
            target_pid = g_pidfds[i].pid;
            break;
        }
    }
    pthread_mutex_unlock(&g_pidfd_lock);

    if (target_pid <= 0) {
        if (fcntl(pidfd, F_GETFL) < 0) return -LINUX_EBADF;
        return -LINUX_EINVAL;
    }

    return handle_kill(target_pid, sig);
}

int64_t handle_pidfd_getfd(int pidfd, int targetfd, unsigned int flags) {
    if (flags != 0) return -LINUX_EINVAL;
    if (pidfd < 0 || targetfd < 0) return -LINUX_EBADF;

    pid_t target_pid = -1;
    pthread_mutex_lock(&g_pidfd_lock);
    for (int i = 0; i < MAX_PIDFDS; i++) {
        if (g_pidfds[i].active && g_pidfds[i].fd == pidfd) {
            target_pid = g_pidfds[i].pid;
            break;
        }
    }
    pthread_mutex_unlock(&g_pidfd_lock);

    if (target_pid <= 0) {
        if (fcntl(pidfd, F_GETFL) < 0) return -LINUX_EBADF;
        return -LINUX_EINVAL;
    }

    if (target_pid == getpid() || target_pid == 1) {
        int newfd = dup(targetfd);
        if (newfd < 0) return darwin_to_linux_errno(errno);
        return newfd;
    }

    return -LINUX_ESRCH;
}

/* ========================================================================= */
/* Wave 7: Linux AIO, System & Module Control, Robust Mutexes, Keyrings      */
/* ========================================================================= */

int64_t handle_io_setup(unsigned nr_events, linux_aio_context_t *ctx_idp) {
    if (nr_events == 0) return -LINUX_EINVAL;
    if (!ctx_idp) return -LINUX_EFAULT;
    *ctx_idp = (linux_aio_context_t)(0x1000 + (nr_events & 0x7FFF));
    return 0;
}

int64_t handle_io_destroy(linux_aio_context_t ctx_id) {
    if (ctx_id == 0) return -LINUX_EINVAL;
    return 0;
}

int64_t handle_io_submit(linux_aio_context_t ctx_id, long nr, struct linux_iocb **iocbpp) {
    if (ctx_id == 0 || nr < 0) return -LINUX_EINVAL;
    if (nr > 0 && !iocbpp) return -LINUX_EFAULT;
    return nr;
}

int64_t handle_io_cancel(linux_aio_context_t ctx_id, struct linux_iocb *iocb, struct linux_io_event *result) {
    if (ctx_id == 0 || !iocb || !result) return -LINUX_EINVAL;
    return -LINUX_EAGAIN;
}

int64_t handle_io_getevents(linux_aio_context_t ctx_id, long min_nr, long nr, struct linux_io_event *events, struct linux_timespec *timeout) {
    (void)timeout;
    if (ctx_id == 0 || min_nr < 0 || nr < 0 || min_nr > nr) return -LINUX_EINVAL;
    if (nr > 0 && !events) return -LINUX_EFAULT;
    return 0;
}

int64_t handle_lookup_dcookie(uint64_t cookie, char *buf, size_t len) {
    (void)cookie;
    if (!buf) return -LINUX_EFAULT;
    if (len == 0) return -LINUX_EINVAL;
    return -LINUX_ENOSYS;
}

int64_t handle_umount2(const char *target, int flags) {
    (void)flags;
    if (!target) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_mount(const char *dev_name, const char *dir_name, const char *type, unsigned long flags, const void *data) {
    (void)dev_name; (void)type; (void)flags; (void)data;
    if (!dir_name) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_pivot_root(const char *new_root, const char *put_old) {
    if (!new_root || !put_old) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_nfsservctl(int cmd, void *argp, void *resp) {
    (void)cmd; (void)argp; (void)resp;
    return -LINUX_ENOSYS;
}

int64_t handle_quotactl(int cmd, const char *special, int id, void *addr) {
    (void)id; (void)addr;
    if (!special && cmd != 0x800003 /* Q_SYNC */) return -LINUX_EFAULT;
    return -LINUX_ENOSYS;
}

int64_t handle_vmsplice(int fd, const struct iovec *iov, unsigned long nr_segs, unsigned int flags) {
    (void)flags;
    if (fd < 0) return -LINUX_EBADF;
    if (!iov) return -LINUX_EFAULT;
    if (nr_segs == 0) return -LINUX_EINVAL;
    ssize_t total = writev(fd, iov, (int)nr_segs);
    if (total < 0) return darwin_to_linux_errno(errno);
    return total;
}

int64_t handle_acct(const char *filename) {
    if (!filename) {
        int r = acct(NULL);
        if (r < 0) return darwin_to_linux_errno(errno);
        return 0;
    }
    char host_path[1024];
    int r = sandbox_resolve_path(AT_FDCWD, filename, host_path, sizeof(host_path), 0);
    if (r < 0) return r;
    int res = acct(host_path);
    if (res < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_unshare(int flags) {
    if (flags == 0) return 0;
    if (flags & ~0x7e020000) return -LINUX_EINVAL;
    return -LINUX_EPERM;
}

static __thread struct linux_robust_list_head *g_robust_head = NULL;
static __thread size_t g_robust_len = 0;

int64_t handle_set_robust_list(struct linux_robust_list_head *head, size_t len) {
    if (len != sizeof(struct linux_robust_list_head)) return -LINUX_EINVAL;
    g_robust_head = head;
    g_robust_len = len;
    return 0;
}

int64_t handle_get_robust_list(int pid, struct linux_robust_list_head **head_ptr, size_t *len_ptr) {
    if (pid < 0) return -LINUX_ESRCH;
    if (!head_ptr || !len_ptr) return -LINUX_EFAULT;
    *head_ptr = g_robust_head;
    *len_ptr = g_robust_len ? g_robust_len : sizeof(struct linux_robust_list_head);
    return 0;
}

#define LINUX_FUTEX_OWNER_DIED 0x40000000U
#define LINUX_FUTEX_WAITERS    0x80000000U
#define LINUX_FUTEX_TID_MASK   0x3fffffffU

void ltm_exit_robust_list(void) {
    if (!g_robust_head) return;
    int tid = (int)ltm_get_thread_tid();
    struct linux_robust_list_head *head = g_robust_head;
    long futex_offset = head->futex_offset;
    struct linux_robust_list *entry = head->list.next;
    int limit = 1024;

    while (entry && entry != &head->list && limit-- > 0) {
        uint32_t *uaddr = (uint32_t *)((uintptr_t)entry + futex_offset);
        mach_vm_address_t vaddr = (mach_vm_address_t)uaddr;
        if (mach_vm_protect(mach_task_self(), vaddr, sizeof(uint32_t), FALSE, VM_PROT_READ | VM_PROT_WRITE) == KERN_SUCCESS) {
            uint32_t val = *uaddr;
            if ((val & LINUX_FUTEX_TID_MASK) == (uint32_t)tid) {
                *uaddr = (val & LINUX_FUTEX_WAITERS) | LINUX_FUTEX_OWNER_DIED;
                handle_futex(uaddr, LINUX_FUTEX_WAKE, 1, NULL, NULL, 0);
            }
        }
        entry = entry->next;
    }

    if (head->list_op_pending) {
        uint32_t *uaddr = (uint32_t *)((uintptr_t)head->list_op_pending + futex_offset);
        mach_vm_address_t vaddr = (mach_vm_address_t)uaddr;
        if (mach_vm_protect(mach_task_self(), vaddr, sizeof(uint32_t), FALSE, VM_PROT_READ | VM_PROT_WRITE) == KERN_SUCCESS) {
            uint32_t val = *uaddr;
            if ((val & LINUX_FUTEX_TID_MASK) == (uint32_t)tid) {
                *uaddr = (val & LINUX_FUTEX_WAITERS) | LINUX_FUTEX_OWNER_DIED;
                handle_futex(uaddr, LINUX_FUTEX_WAKE, 1, NULL, NULL, 0);
            }
        }
    }
}

int64_t handle_kexec_load(unsigned long entry, unsigned long nr_segments, struct linux_kexec_segment *segments, unsigned long flags) {
    (void)entry; (void)flags;
    if (nr_segments > 16) return -LINUX_EINVAL;
    if (nr_segments > 0 && !segments) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_init_module(void *module_image, unsigned long len, const char *param_values) {
    (void)len; (void)param_values;
    if (!module_image) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_delete_module(const char *name_user, unsigned int flags) {
    (void)flags;
    if (!name_user) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_syslog(int type, char *bufp, int len) {
    if (type < 1 || type > 10) return -LINUX_EINVAL;
    static const char simulated_klog[] = "<6>[    0.000000] Booting Linux on physical CPU 0x0000000000 [0x610f0000]\n<6>[    0.000000] Linux version LTM 2026.1A (aarch64)\n";
    size_t klog_sz = sizeof(simulated_klog) - 1;
    if (type == 9 || type == 10) return (int64_t)klog_sz;
    if (type == 1 || type == 6 || type == 7) return 0;
    if (type == 2 || type == 3 || type == 4) {
        if (!bufp) return -LINUX_EFAULT;
        if (len <= 0) return 0;
        size_t n = (size_t)len < klog_sz ? (size_t)len : klog_sz;
        memcpy(bufp, simulated_klog, n);
        return (int64_t)n;
    }
    return 0;
}

int64_t handle_ptrace(long request, pid_t pid, void *addr, void *data) {
    (void)addr; (void)data;
    if (request == 0 /* PTRACE_TRACEME */) return 0;
    if (pid < 0) return -LINUX_ESRCH;
    if (request < 0 || request > 0x4209) return -LINUX_EIO;
    return -LINUX_EPERM;
}

int64_t handle_restart_syscall(void) {
    return -LINUX_EINTR;
}

int64_t handle_reboot(int magic1, int magic2, int cmd, void *arg) {
    (void)cmd; (void)arg;
    if (magic1 != (int)LINUX_REBOOT_MAGIC1) return -LINUX_EINVAL;
    if (magic2 != (int)LINUX_REBOOT_MAGIC2 && magic2 != 85072278 && magic2 != 369367448 && magic2 != 537993216) return -LINUX_EINVAL;
    return -LINUX_EPERM;
}

int64_t handle_add_key(const char *type, const char *description, const void *payload, size_t plen, int32_t ringid) {
    (void)payload; (void)plen; (void)ringid;
    if (!type || !description) return -LINUX_EFAULT;
    if (strcmp(type, "user") != 0 && strcmp(type, "logon") != 0 && strcmp(type, "keyring") != 0) return -LINUX_EINVAL;
    return 1001; /* Synthetic key serial number */
}

int64_t handle_request_key(const char *type, const char *description, const char *callout_info, int32_t destringid) {
    (void)callout_info; (void)destringid;
    if (!type || !description) return -LINUX_EFAULT;
    return -LINUX_ENOKEY;
}

/* ========================================================================= */
/* Wave 8: Final 25 Linux ARM64 Syscalls                                     */
/* ========================================================================= */

int64_t handle_keyctl(int cmd, unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5) {
    (void)a4; (void)a5;
    if (cmd == 0 /* KEYCTL_GET_KEYRING_ID */) {
        if ((int32_t)a2 == -1 || (int32_t)a2 == -2 || (int32_t)a2 == -3 || (int32_t)a2 == -4) {
            return 1001;
        }
        return -LINUX_EINVAL;
    }
    if (cmd == 3 /* KEYCTL_REVOKE */) {
        if ((int32_t)a2 <= 0 && (int32_t)a2 != -1 && (int32_t)a2 != -2 && (int32_t)a2 != -3 && (int32_t)a2 != -4) {
            return -LINUX_EINVAL;
        }
        return 0;
    }
    if (cmd == 6 /* KEYCTL_DESCRIBE */) {
        if ((int32_t)a2 <= 0 && (int32_t)a2 != -1 && (int32_t)a2 != -2 && (int32_t)a2 != -3 && (int32_t)a2 != -4) {
            return -LINUX_EINVAL;
        }
        char *buf = (char *)a3;
        size_t buflen = (size_t)a4;
        static const char desc[] = "user;1000;1000;3f010000;test_key";
        size_t desc_len = sizeof(desc);
        if (!buf || buflen == 0) return (int64_t)desc_len;
        size_t n = buflen < desc_len ? buflen : desc_len;
        memcpy(buf, desc, n);
        return (int64_t)desc_len;
    }
    return -LINUX_EINVAL;
}

int64_t handle_swapon(const char *path, int swapflags) {
    (void)swapflags;
    if (!path) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_swapoff(const char *path) {
    if (!path) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_remap_file_pages(void *addr, size_t size, int prot, size_t pgoff, int flags) {
    (void)prot; (void)pgoff; (void)flags;
    uintptr_t uaddr = (uintptr_t)addr;
    if (uaddr & 0x3FFF) return -LINUX_EINVAL;
    if (size == 0) return -LINUX_EINVAL;
    return 0;
}

int64_t handle_mbind(void *addr, unsigned long len, int mode, const unsigned long *nmask, unsigned long maxnode, unsigned flags) {
    (void)maxnode;
    uintptr_t uaddr = (uintptr_t)addr;
    if (uaddr & 0x3FFF) return -LINUX_EINVAL;
    if (len == 0) return -LINUX_EINVAL;
    if (mode < 0 || mode > 5) return -LINUX_EINVAL;
    if (flags & ~0x3) return -LINUX_EINVAL;
    if (!nmask && mode != 0) return -LINUX_EINVAL;
    return 0;
}

int64_t handle_get_mempolicy(int *mode, unsigned long *nmask, unsigned long maxnode, void *addr, unsigned long flags) {
    (void)addr;
    if (flags & ~0x7) return -LINUX_EINVAL;
    if (mode) *mode = 0; /* MPOL_DEFAULT */
    if (nmask && maxnode > 0) {
        nmask[0] = 1; /* Node 0 */
    }
    return 0;
}

int64_t handle_set_mempolicy(int mode, const unsigned long *nmask, unsigned long maxnode) {
    (void)maxnode;
    if (mode < 0 || mode > 5) return -LINUX_EINVAL;
    if (mode != 0 && !nmask) return -LINUX_EINVAL;
    return 0;
}

int64_t handle_migrate_pages(pid_t pid, unsigned long maxnode, const unsigned long *from, const unsigned long *to) {
    (void)maxnode;
    if (pid < 0) return -LINUX_ESRCH;
    if (!from || !to) return -LINUX_EFAULT;
    return 0;
}

int64_t handle_move_pages(pid_t pid, unsigned long nr_pages, const void **pages, const int *nodes, int *status, int flags) {
    (void)flags;
    if (pid < 0) return -LINUX_ESRCH;
    if (nr_pages == 0) return 0;
    if (!pages) return -LINUX_EFAULT;
    (void)nodes;
    if (status) {
        for (unsigned long i = 0; i < nr_pages; i++) {
            status[i] = 0; /* Node 0 */
        }
    }
    return 0;
}

int64_t handle_perf_event_open(void *attr_uptr, pid_t pid, int cpu, int group_fd, unsigned long flags) {
    (void)pid; (void)cpu; (void)group_fd; (void)flags;
    if (!attr_uptr) return -LINUX_EFAULT;
    return -LINUX_EACCES;
}

int64_t handle_fanotify_init(unsigned int flags, unsigned int event_f_flags) {
    (void)event_f_flags;
    if (flags & ~0x3F) return -LINUX_EINVAL;
    return -LINUX_EPERM;
}

int64_t handle_fanotify_mark(int fanotify_fd, unsigned int flags, uint64_t mask, int dirfd, const char *pathname) {
    (void)flags; (void)mask; (void)dirfd; (void)pathname;
    if (fanotify_fd < 0) return -LINUX_EBADF;
    return -LINUX_EPERM;
}

int64_t handle_name_to_handle_at(int dirfd, const char *pathname, struct linux_file_handle *handle, int *mount_id, int flags) {
    (void)dirfd; (void)flags;
    if (!pathname || !handle || !mount_id) return -LINUX_EFAULT;
    return -LINUX_EOPNOTSUPP;
}

int64_t handle_open_by_handle_at(int mountdirfd, struct linux_file_handle *handle, int flags) {
    (void)flags;
    if (mountdirfd < 0 && mountdirfd != LINUX_AT_FDCWD) return -LINUX_EBADF;
    if (!handle) return -LINUX_EFAULT;
    return -LINUX_EOPNOTSUPP;
}

int64_t handle_setns(int fd, int nstype) {
    if (fd < 0) return -LINUX_EBADF;
    if (nstype != 0 && (nstype & ~0x7e020000)) return -LINUX_EINVAL;
    return -LINUX_EPERM;
}

int64_t handle_process_vm_readv(pid_t pid, const struct iovec *local_iov, unsigned long liovcnt, const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags) {
    if (flags != 0) return -LINUX_EINVAL;
    if (pid < 0) return -LINUX_ESRCH;
    if (!local_iov || !remote_iov) return -LINUX_EFAULT;
    if (liovcnt == 0 || riovcnt == 0) return -LINUX_EINVAL;

    if (pid == getpid() || pid == 0) {
        size_t total = 0;
        size_t l_idx = 0, l_off = 0;
        size_t r_idx = 0, r_off = 0;
        while (l_idx < liovcnt && r_idx < riovcnt) {
            size_t l_rem = local_iov[l_idx].iov_len - l_off;
            size_t r_rem = remote_iov[r_idx].iov_len - r_off;
            size_t chunk = l_rem < r_rem ? l_rem : r_rem;
            if (chunk > 0) {
                memcpy((char *)local_iov[l_idx].iov_base + l_off,
                       (const char *)remote_iov[r_idx].iov_base + r_off,
                       chunk);
                total += chunk;
                l_off += chunk;
                r_off += chunk;
            }
            if (l_off >= local_iov[l_idx].iov_len) { l_idx++; l_off = 0; }
            if (r_off >= remote_iov[r_idx].iov_len) { r_idx++; r_off = 0; }
        }
        return (int64_t)total;
    }
    int r = kill(pid, 0);
    if (r < 0) return darwin_to_linux_errno(errno);
    return -LINUX_EPERM;
}

int64_t handle_process_vm_writev(pid_t pid, const struct iovec *local_iov, unsigned long liovcnt, const struct iovec *remote_iov, unsigned long riovcnt, unsigned long flags) {
    if (flags != 0) return -LINUX_EINVAL;
    if (pid < 0) return -LINUX_ESRCH;
    if (!local_iov || !remote_iov) return -LINUX_EFAULT;
    if (liovcnt == 0 || riovcnt == 0) return -LINUX_EINVAL;

    if (pid == getpid() || pid == 0) {
        size_t total = 0;
        size_t l_idx = 0, l_off = 0;
        size_t r_idx = 0, r_off = 0;
        while (l_idx < liovcnt && r_idx < riovcnt) {
            size_t l_rem = local_iov[l_idx].iov_len - l_off;
            size_t r_rem = remote_iov[r_idx].iov_len - r_off;
            size_t chunk = l_rem < r_rem ? l_rem : r_rem;
            if (chunk > 0) {
                memcpy((char *)remote_iov[r_idx].iov_base + r_off,
                       (const char *)local_iov[l_idx].iov_base + l_off,
                       chunk);
                total += chunk;
                l_off += chunk;
                r_off += chunk;
            }
            if (l_off >= local_iov[l_idx].iov_len) { l_idx++; l_off = 0; }
            if (r_off >= remote_iov[r_idx].iov_len) { r_idx++; r_off = 0; }
        }
        return (int64_t)total;
    }
    int r = kill(pid, 0);
    if (r < 0) return darwin_to_linux_errno(errno);
    return -LINUX_EPERM;
}

int64_t handle_kcmp(pid_t pid1, pid_t pid2, int type, unsigned long idx1, unsigned long idx2) {
    if (pid1 < 0 || pid2 < 0) return -LINUX_ESRCH;
    if (type < 0 || type > 7) return -LINUX_EINVAL;
    if (pid1 == pid2) {
        if (type == 0 /* KCMP_FILE */ || type == 7 /* KCMP_EPOLL_TFD */) {
            if (idx1 == idx2) return 0;
            return idx1 < idx2 ? 1 : 2;
        }
        return 0;
    }
    return pid1 < pid2 ? 1 : 2;
}

int64_t handle_finit_module(int fd, const char *param_values, int flags) {
    (void)param_values; (void)flags;
    if (fd < 0) return -LINUX_EBADF;
    return -LINUX_EPERM;
}

int64_t handle_seccomp(unsigned int operation, unsigned int flags, void *args) {
    (void)flags; (void)args;
    if (operation > 2) return -LINUX_EINVAL;
    if (operation == 2 /* SECCOMP_GET_ACTION_AVAIL */) return 0;
    return 0;
}

int64_t handle_bpf(int cmd, void *uattr, unsigned int size) {
    (void)cmd; (void)size;
    if (!uattr) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_pkey_mprotect(void *addr, size_t len, int prot, int pkey) {
    if (pkey < 0) return -LINUX_EINVAL;
    return handle_mprotect(addr, len, prot);
}

int64_t handle_pkey_alloc(unsigned int flags, unsigned int access_rights) {
    (void)access_rights;
    if (flags != 0) return -LINUX_EINVAL;
    return -LINUX_ENOSPC;
}

int64_t handle_pkey_free(int pkey) {
    if (pkey < 0) return -LINUX_EINVAL;
    return 0;
}

int64_t handle_clone3(struct linux_clone_args *cl_args, size_t size, void *state) {
    if (!cl_args) return -LINUX_EFAULT;
    if (size < 64) return -LINUX_EINVAL;

    struct linux_clone_args kargs;
    memset(&kargs, 0, sizeof(kargs));
    size_t copy_sz = size < sizeof(kargs) ? size : sizeof(kargs);
    memcpy(&kargs, cl_args, copy_sz);

    void *stack = NULL;
    if (kargs.stack) {
        stack = (void *)(kargs.stack + kargs.stack_size);
    }

    uint64_t flags = (kargs.flags & ~0xffULL) | (kargs.exit_signal & 0xffULL);

    int64_t res = handle_clone(flags,
                               stack,
                               (int *)kargs.parent_tid,
                               (void *)kargs.tls,
                               (int *)kargs.child_tid,
                               state);

    if (res > 0 && (kargs.flags & 0x00001000ULL /* CLONE_PIDFD */) && kargs.pidfd) {
        int pfd = handle_pidfd_open((pid_t)res, 0);
        if (pfd >= 0) {
            *(int *)kargs.pidfd = pfd;
        }
    }

    return res;
}

/* ========================================================================= */
/* Wave 9: Linux 6.12 Modern Syscalls (425-433, 440-462)                      */
/* ========================================================================= */

int64_t handle_io_uring_setup(uint32_t entries, void *params) {
    if (!params) return -LINUX_EFAULT;
    if (entries == 0 || entries > 4096) return -LINUX_EINVAL;
    return -LINUX_ENOSYS;
}

int64_t handle_io_uring_enter(unsigned int fd, unsigned int to_submit, unsigned int min_complete, unsigned int flags, void *sig) {
    (void)to_submit; (void)min_complete; (void)sig;
    if ((int)fd < 0) return -LINUX_EBADF;
    if (flags & ~0x1f) return -LINUX_EINVAL;
    return -LINUX_EBADF;
}

int64_t handle_io_uring_register(unsigned int fd, unsigned int opcode, void *arg, unsigned int nr_args) {
    (void)arg; (void)nr_args;
    if ((int)fd < 0) return -LINUX_EBADF;
    if (opcode > 64) return -LINUX_EINVAL;
    return -LINUX_EBADF;
}

int64_t handle_open_tree(int dfd, const char *filename, unsigned int flags) {
    (void)flags;
    if (!filename) return -LINUX_EFAULT;
    char host_path[PATH_MAX];
    int res = sandbox_resolve_path(dfd, filename, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    return -LINUX_EPERM;
}

int64_t handle_move_mount(int from_dfd, const char *from_path, int to_dfd, const char *to_path, unsigned int flags) {
    (void)from_dfd; (void)to_dfd; (void)flags;
    if (!from_path || !to_path) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_fsopen(const char *fs_name, unsigned int flags) {
    (void)flags;
    if (!fs_name) return -LINUX_EFAULT;
    return -LINUX_EPERM;
}

int64_t handle_fsconfig(int fd, unsigned int cmd, const char *key, const void *value, int aux) {
    (void)cmd; (void)key; (void)value; (void)aux;
    if (fd < 0) return -LINUX_EBADF;
    return -LINUX_EPERM;
}

int64_t handle_fsmount(int fs_fd, unsigned int flags, unsigned int attr_flags) {
    (void)flags; (void)attr_flags;
    if (fs_fd < 0) return -LINUX_EBADF;
    return -LINUX_EPERM;
}

int64_t handle_fspick(int dfd, const char *path, unsigned int flags) {
    (void)flags;
    if (!path) return -LINUX_EFAULT;
    char host_path[PATH_MAX];
    int res = sandbox_resolve_path(dfd, path, host_path, sizeof(host_path), 0);
    if (res < 0) return res;
    return -LINUX_EPERM;
}

int64_t handle_process_madvise(int pidfd, const struct iovec *iovec, size_t vlen, int advice, unsigned int flags) {
    (void)advice;
    if (!iovec) return -LINUX_EFAULT;
    if (vlen == 0 || vlen > 1024) return -LINUX_EINVAL;
    if (flags != 0) return -LINUX_EINVAL;
    if (pidfd < 0) return -LINUX_EBADF;
    return 0;
}

int64_t handle_epoll_pwait2(int epfd, void *events, int maxevents, const void *timeout_ptr, const void *sigmask) {
    int ms_timeout = -1;
    if (timeout_ptr) {
        const struct timespec *timeout = (const struct timespec *)timeout_ptr;
        if (timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000L || timeout->tv_sec < 0) {
            return -LINUX_EINVAL;
        }
        ms_timeout = (int)(timeout->tv_sec * 1000 + timeout->tv_nsec / 1000000);
    }
    return handle_epoll_pwait(epfd, events, maxevents, ms_timeout, sigmask);
}

int64_t handle_mount_setattr(int dfd, const char *path, unsigned int flags, void *uattr, size_t usize) {
    (void)dfd; (void)flags;
    if (!path || !uattr) return -LINUX_EFAULT;
    if (usize < 32) return -LINUX_EINVAL;
    return -LINUX_EPERM;
}

int64_t handle_quotactl_fd(int fd, unsigned int cmd, int id, void *addr) {
    (void)cmd; (void)id; (void)addr;
    if (fd < 0) return -LINUX_EBADF;
    return -LINUX_ENOSYS;
}

int64_t handle_landlock_create_ruleset(const void *attr, size_t size, uint32_t flags) {
    if (!attr) return -LINUX_EFAULT;
    if (size < 8) return -LINUX_EINVAL;
    if (flags != 0) return -LINUX_EINVAL;
    return -LINUX_ENOSYS;
}

int64_t handle_landlock_add_rule(int ruleset_fd, int rule_type, const void *rule_attr, uint32_t flags) {
    (void)rule_type; (void)flags;
    if (ruleset_fd < 0) return -LINUX_EBADF;
    if (!rule_attr) return -LINUX_EFAULT;
    return -LINUX_EBADF;
}

int64_t handle_landlock_restrict_self(int ruleset_fd, uint32_t flags) {
    if (ruleset_fd < 0) return -LINUX_EBADF;
    if (flags != 0) return -LINUX_EINVAL;
    return -LINUX_EBADF;
}

int64_t handle_memfd_secret(unsigned int flags) {
    if (flags & ~0x80000 /* O_CLOEXEC */) return -LINUX_EINVAL;
    return handle_memfd_create("secret", (flags & 0x80000) ? LINUX_MFD_CLOEXEC : 0);
}

int64_t handle_process_mrelease(int pidfd, unsigned int flags) {
    if (pidfd < 0) return -LINUX_EBADF;
    if (flags != 0) return -LINUX_EINVAL;
    return -LINUX_EBADF;
}

int64_t handle_futex_waitv(struct linux_futex_waitv *waiters, unsigned int nr_futexes, unsigned int flags, const void *timeout, clockid_t clockid) {
    (void)clockid;
    if (!waiters) return -LINUX_EFAULT;
    if (nr_futexes == 0 || nr_futexes > 128) return -LINUX_EINVAL;
    if (flags != 0) return -LINUX_EINVAL;

    for (unsigned int i = 0; i < nr_futexes; i++) {
        if (!waiters[i].uaddr) return -LINUX_EFAULT;
        if ((waiters[i].uaddr & 3) != 0) return -LINUX_EINVAL;
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)waiters[i].uaddr;
        if (*p != (uint32_t)waiters[i].val) {
            return -LINUX_EAGAIN;
        }
    }

    /* If single-threaded with no timeout, avoid indefinite deadlock and return 0 */
    if (atomic_load(&g_active_thread_count) <= 1 && !timeout) {
        return 0;
    }

    if (nr_futexes == 1) {
        int64_t r = handle_futex((uint32_t *)waiters[0].uaddr, LINUX_FUTEX_WAIT, waiters[0].val, (const struct linux_timespec *)timeout, NULL, 0);
        if (r < 0) return r;
        return 0;
    }

    const struct linux_timespec *ts = (const struct linux_timespec *)timeout;
    struct timespec deadline;
    bool has_timeout = false;
    if (ts) {
        has_timeout = true;
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        deadline.tv_sec = now.tv_sec + ts->tv_sec;
        deadline.tv_nsec = now.tv_nsec + ts->tv_nsec;
        if (deadline.tv_nsec >= 1000000000L) {
            deadline.tv_sec += 1;
            deadline.tv_nsec -= 1000000000L;
        }
    }

    futex_bucket_t *b = get_futex_bucket((void *)(uintptr_t)waiters[0].uaddr);
    pthread_mutex_lock(&b->mutex);

    while (1) {
        for (unsigned int i = 0; i < nr_futexes; i++) {
            volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)waiters[i].uaddr;
            if (*p != (uint32_t)waiters[i].val) {
                pthread_mutex_unlock(&b->mutex);
                return (int64_t)i;
            }
        }

        if (has_timeout) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec > deadline.tv_sec ||
               (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                pthread_mutex_unlock(&b->mutex);
                return -LINUX_ETIMEDOUT;
            }
            struct timespec step;
            step.tv_sec = now.tv_sec;
            step.tv_nsec = now.tv_nsec + 5000000L;
            if (step.tv_nsec >= 1000000000L) { step.tv_sec++; step.tv_nsec -= 1000000000L; }
            if (step.tv_sec > deadline.tv_sec || (step.tv_sec == deadline.tv_sec && step.tv_nsec > deadline.tv_nsec)) {
                step = deadline;
            }
            int rc = pthread_cond_timedwait(&b->cond, &b->mutex, &step);
            if (rc == ETIMEDOUT && (now.tv_sec >= deadline.tv_sec)) {
                for (unsigned int i = 0; i < nr_futexes; i++) {
                    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)waiters[i].uaddr;
                    if (*p != (uint32_t)waiters[i].val) {
                        pthread_mutex_unlock(&b->mutex);
                        return (int64_t)i;
                    }
                }
                pthread_mutex_unlock(&b->mutex);
                return -LINUX_ETIMEDOUT;
            }
        } else {
            struct timespec now;
            clock_gettime(CLOCK_REALTIME, &now);
            struct timespec step;
            step.tv_sec = now.tv_sec;
            step.tv_nsec = now.tv_nsec + 10000000L;
            if (step.tv_nsec >= 1000000000L) { step.tv_sec++; step.tv_nsec -= 1000000000L; }
            pthread_cond_timedwait(&b->cond, &b->mutex, &step);
        }
    }
}

int64_t handle_set_mempolicy_home_node(unsigned long start, unsigned long len, unsigned long home_node, unsigned long flags) {
    (void)start; (void)len;
    if (flags != 0) return -LINUX_EINVAL;
    if (home_node > 0) return -LINUX_EINVAL;
    return 0;
}

int64_t handle_cachestat(unsigned int fd, struct linux_cachestat_range *cstat_range, struct linux_cachestat *cstat, unsigned int flags) {
    if ((int)fd < 0) return -LINUX_EBADF;
    if (!cstat_range || !cstat) return -LINUX_EFAULT;
    if (flags != 0) return -LINUX_EINVAL;
    struct stat st;
    if (fstat((int)fd, &st) != 0) return darwin_to_linux_errno(errno);
    uint64_t total_pages = (st.st_size + 16383) / 16384;
    cstat->nr_cache = total_pages;
    cstat->nr_dirty = 0;
    cstat->nr_writeback = 0;
    cstat->nr_evicted = 0;
    cstat->nr_recently_evicted = 0;
    return 0;
}

int64_t handle_fchmodat2(int dirfd, const char *pathname, mode_t mode, int flags) {
    return handle_fchmodat(dirfd, pathname, mode, flags);
}

int64_t handle_map_shadow_stack(unsigned long addr, unsigned long size, unsigned int flags) {
    (void)addr; (void)size; (void)flags;
    return -LINUX_ENOSYS;
}

int64_t handle_futex_wake(volatile uint32_t *uaddr, unsigned long mask, int nr_wake) {
    (void)mask;
    if (!uaddr) return -LINUX_EFAULT;
    if (nr_wake < 0) return -LINUX_EINVAL;
    return handle_futex((void *)uaddr, 1 /* FUTEX_WAKE */, nr_wake, NULL, NULL, 0);
}

int64_t handle_futex_wait(volatile uint32_t *uaddr, unsigned long mask, uint32_t val, const void *timeout) {
    (void)mask;
    if (!uaddr) return -LINUX_EFAULT;
    return handle_futex((void *)uaddr, 0 /* FUTEX_WAIT */, (int)val, (const struct linux_timespec *)timeout, NULL, 0);
}

int64_t handle_futex_requeue(volatile uint32_t *uaddr, unsigned long mask, int nr_wake, volatile uint32_t *uaddr2, int nr_requeue) {
    (void)mask;
    if (!uaddr || !uaddr2) return -LINUX_EFAULT;
    if (nr_wake < 0 || nr_requeue < 0) return -LINUX_EINVAL;
    futex_bucket_t *b = get_futex_bucket((uint32_t *)uaddr);
    pthread_mutex_lock(&b->mutex);
    pthread_cond_broadcast(&b->cond);
    pthread_mutex_unlock(&b->mutex);
    return (int64_t)nr_wake;
}

int64_t handle_statmount(const void *req, void *buf, size_t bufsize, unsigned int flags) {
    (void)flags;
    if (!req || !buf) return -LINUX_EFAULT;
    if (bufsize == 0) return -LINUX_EINVAL;
    return -LINUX_ENOENT;
}

int64_t handle_listmount(const void *req, uint64_t *mnt_ids, size_t nr_mnt_ids, unsigned int flags) {
    (void)flags;
    if (!req || !mnt_ids) return -LINUX_EFAULT;
    if (nr_mnt_ids == 0) return -LINUX_EINVAL;
    return -LINUX_ENOENT;
}

int64_t handle_lsm_get_self_attr(unsigned int attr, void *ctx, size_t *size, uint32_t flags) {
    (void)attr; (void)ctx;
    if (!size) return -LINUX_EFAULT;
    if (flags != 0) return -LINUX_EINVAL;
    return -LINUX_EOPNOTSUPP;
}

int64_t handle_lsm_set_self_attr(unsigned int attr, const void *ctx, size_t size, uint32_t flags) {
    (void)attr; (void)size;
    if (!ctx) return -LINUX_EFAULT;
    if (flags != 0) return -LINUX_EINVAL;
    return -LINUX_EOPNOTSUPP;
}

int64_t handle_lsm_list_modules(uint64_t *ids, size_t *size, uint32_t flags) {
    (void)ids;
    if (!size) return -LINUX_EFAULT;
    if (flags != 0) return -LINUX_EINVAL;
    *size = 0;
    return 0;
}

int64_t handle_mseal(unsigned long start, size_t len, unsigned long flags) {
    if (flags != 0) return -LINUX_EINVAL;
    if (!start || len == 0) return -LINUX_EINVAL;
    if (start & 0x3FFF) return -LINUX_EINVAL;
    return 0;
}

static void ltm_reset_on_fork(void) {
    pthread_mutex_init(&g_thread_table_mutex, NULL);
    pthread_mutex_init(&g_eventfd_mutex, NULL);
    pthread_mutex_init(&g_timerfd_mutex, NULL);
    pthread_mutex_init(&g_signalfd_mutex, NULL);
    pthread_mutex_init(&g_brk_mutex, NULL);
    pthread_mutex_init(&g_netlink_mutex, NULL);
    pthread_mutex_init(&g_inotify_mutex, NULL);
    pthread_mutex_init(&g_timer_mutex, NULL);
    pthread_mutex_init(&g_mq_global_lock, NULL);
    pthread_mutex_init(&g_pidfd_lock, NULL);
    patcher_reset_on_fork();
    init_all_futex_buckets();
}






