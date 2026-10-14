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

/* Track ICMP sockets to strip IPv4 header on receive */
#define MAX_TRACKED_FDS 1024
static bool g_sock_is_icmp[MAX_TRACKED_FDS];

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

int64_t handle_close(int fd) {
    if (fd >= 0 && fd < DIR_CACHE_MAX && g_dir_cache[fd]) {
        closedir(g_dir_cache[fd]);
        g_dir_cache[fd] = NULL;
    }
    if (fd >= 0 && fd < MAX_TRACKED_FDS) {
        g_sock_is_icmp[fd] = false;
    }
    ltm_epoll_close(fd);
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
        if (pgrp < 0) {
            pgrp = getpgrp();
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

    /* FIONREAD: Linux 0x541b */
    if (request == 0x541b) {
        if (!arg) return -LINUX_EFAULT;
        int n = 0;
        if (ioctl(fd, FIONREAD, &n) != 0) return darwin_to_linux_errno(errno);
        *(int *)arg = n;
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
    if (ret >= 0 && ret < MAX_TRACKED_FDS && oldfd >= 0 && oldfd < MAX_TRACKED_FDS) {
        g_sock_is_icmp[ret] = g_sock_is_icmp[oldfd];
    }
    return ret;
}

int64_t handle_dup3(int oldfd, int newfd, int flags) {
    if (oldfd == newfd) return -LINUX_EINVAL;
    int ret = dup2(oldfd, newfd);
    if (ret < 0) return darwin_to_linux_errno(errno);

    if (newfd >= 0 && newfd < MAX_TRACKED_FDS && oldfd >= 0 && oldfd < MAX_TRACKED_FDS) {
        g_sock_is_icmp[newfd] = g_sock_is_icmp[oldfd];
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
    if (ret < 0) return darwin_to_linux_errno(errno);

    if ((cmd == F_DUPFD || cmd == 1030) && ret >= 0 && ret < MAX_TRACKED_FDS && fd >= 0 && fd < MAX_TRACKED_FDS) {
        g_sock_is_icmp[ret] = g_sock_is_icmp[fd];
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

#define LINUX_NSIG 65
static struct linux_sigaction g_guest_sigactions[LINUX_NSIG];

struct ltm_sigframe {
    arm_thread_state64_t saved_state;
    uint64_t magic;
};

#define LTM_SIGFRAME_MAGIC 0x4c544d5f534947ULL /* "LTM_SIG" */

static void ltm_host_signal_handler(int sig, siginfo_t *info, void *ucontext) {
    (void)info;
    if (sig <= 0 || sig >= LINUX_NSIG) return;

    struct linux_sigaction *act = &g_guest_sigactions[sig];
    if (!act->l_handler || act->l_handler == (void (*)(int))SIG_DFL || act->l_handler == (void (*)(int))SIG_IGN) {
        return;
    }

    ucontext_t *uc = (ucontext_t *)ucontext;
    arm_thread_state64_t *cur_state = &uc->uc_mcontext->__ss;

    /* Push saved state onto guest stack */
    uintptr_t sp = cur_state->__sp;
    sp -= sizeof(struct ltm_sigframe);
    sp &= ~0xfULL;

    struct ltm_sigframe *frame = (struct ltm_sigframe *)sp;
    memcpy(&frame->saved_state, cur_state, sizeof(arm_thread_state64_t));
    frame->magic = LTM_SIGFRAME_MAGIC;

    /* Setup registers to execute guest signal handler */
    arm_thread_state64_t target_state;
    memset(&target_state, 0, sizeof(target_state));
    target_state.__x[0] = sig;
    target_state.__x[1] = 0;
    target_state.__x[2] = sp;
    target_state.__sp = sp;
    target_state.__lr = act->l_restorer ? (uintptr_t)act->l_restorer : (uintptr_t)ltm_sigreturn_stub;
    target_state.__pc = (uintptr_t)act->l_handler;

    /* Unmask the signal so subsequent signals (like periodic SIGALRM) can be delivered */
    sigset_t set;
    sigemptyset(&set);
    pthread_sigmask(SIG_SETMASK, &set, NULL);

    /* Resume execution into guest handler bypassing Darwin signal trampoline */
    ltm_resume_context(&target_state);
}

int64_t handle_rt_sigaction(int signum, const struct linux_sigaction *act, struct linux_sigaction *oldact, size_t sigsetsize) {
    (void)sigsetsize;
    if (signum <= 0 || signum >= LINUX_NSIG) return -LINUX_EINVAL;
    if (signum == SIGKILL || signum == SIGSTOP) return -LINUX_EINVAL;
    if (signum == SIGTRAP) return 0; /* Keep SIGTRAP for LTM dispatcher */

    if (oldact) {
        *oldact = g_guest_sigactions[signum];
    }

    if (act) {
        g_guest_sigactions[signum] = *act;

        struct sigaction d_sa;
        memset(&d_sa, 0, sizeof(d_sa));

        if (act->l_handler == (void (*)(int))SIG_DFL) {
            d_sa.sa_handler = SIG_DFL;
            sigaction(signum, &d_sa, NULL);
        } else if (act->l_handler == (void (*)(int))SIG_IGN) {
            d_sa.sa_handler = SIG_IGN;
            sigaction(signum, &d_sa, NULL);
        } else {
            d_sa.sa_sigaction = ltm_host_signal_handler;
            d_sa.sa_flags = SA_SIGINFO | SA_NODEFER;
            sigemptyset(&d_sa.sa_mask);
            sigaction(signum, &d_sa, NULL);
        }
    }

    return 0;
}

int64_t handle_rt_sigreturn(void *state_ptr) {
    arm_thread_state64_t *cur_state = (arm_thread_state64_t *)state_ptr;
    uintptr_t sp = cur_state->__sp;
    struct ltm_sigframe *frame = (struct ltm_sigframe *)sp;

    if (frame->magic != LTM_SIGFRAME_MAGIC) {
        ltm_log_error("rt_sigreturn: invalid frame magic 0x%llx", (unsigned long long)frame->magic);
        return -LINUX_EINVAL;
    }

    /* Ensure signals are not masked when returning to main execution */
    sigset_t set;
    sigemptyset(&set);
    pthread_sigmask(SIG_SETMASK, &set, NULL);

    ltm_resume_context(&frame->saved_state);
    return 0; /* Unreachable */
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
    if (ruid) *ruid = 0;
    if (euid) *euid = 0;
    if (suid) *suid = 0;
    return 0;
}

int64_t handle_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid) {
    if (rgid) *rgid = 0;
    if (egid) *egid = 0;
    if (sgid) *sgid = 0;
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
    (void)size;
    (void)list;
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
    return getpid();
}

int64_t handle_uname(struct linux_utsname *buf) {
    if (!buf) return -LINUX_EFAULT;
    memset(buf, 0, sizeof(*buf));

    strncpy(buf->sysname, "Linux", sizeof(buf->sysname) - 1);
    strncpy(buf->nodename, "ltm-mac", sizeof(buf->nodename) - 1);
    strncpy(buf->release, LTM_KERNEL_VERSION_NAME, sizeof(buf->release) - 1);
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
    (void)flags;
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
    buf->f_type = 0xEF53; /* EXT4_SUPER_MAGIC */
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
    buf->f_type = 0xEF53; /* EXT4_SUPER_MAGIC */
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
        size_t path_len = strlen(dun->sun_path);
        size_t space = *llen > 2 ? *llen - 2 : 0;
        if (path_len >= space) path_len = space > 0 ? space - 1 : 0;
        memcpy((char *)laddr + 2, dun->sun_path, path_len);
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
            case 30: *doptname = SO_ACCEPTCONN; break;
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
    if (dest_addr && addrlen > 0) {
        struct sockaddr_storage daddr;
        socklen_t dlen = sizeof(daddr);
        copy_linux_to_darwin_sockaddr(dest_addr, addrlen, &daddr, &dlen);
        ssize_t ret = sendto(fd, buf, len, flags, (const struct sockaddr *)&daddr, dlen);
        if (ret < 0) return darwin_to_linux_errno(errno);
        return ret;
    }
    ssize_t ret = sendto(fd, buf, len, flags, NULL, 0);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_recvfrom(int fd, void *buf, size_t len, int flags, void *src_addr, socklen_t *addrlen) {
    ssize_t ret;
    if (src_addr && addrlen && *addrlen > 0) {
        struct sockaddr_storage ss;
        socklen_t slen = sizeof(ss);
        ret = recvfrom(fd, buf, len, flags, (struct sockaddr *)&ss, &slen);
        if (ret < 0) return darwin_to_linux_errno(errno);

        socklen_t lnamelen = *addrlen;
        copy_darwin_to_linux_sockaddr(&ss, slen, src_addr, &lnamelen);
        *addrlen = lnamelen;
    } else {
        ret = recvfrom(fd, buf, len, flags, NULL, NULL);
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

    return ret;
}

int64_t handle_sendmsg(int fd, const void *msg, int flags) {
    if (!msg) return -LINUX_EINVAL;
    const struct linux_msghdr *lmsg = (const struct linux_msghdr *)msg;

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

    ssize_t ret = sendmsg(fd, &dmsg, flags);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return ret;
}

int64_t handle_recvmsg(int fd, void *msg, int flags) {
    if (!msg) return -LINUX_EINVAL;
    struct linux_msghdr *lmsg = (struct linux_msghdr *)msg;

    struct msghdr dmsg;
    memset(&dmsg, 0, sizeof(dmsg));
    dmsg.msg_iov = lmsg->msg_iov;
    dmsg.msg_iovlen = (int)lmsg->msg_iovlen;
    dmsg.msg_control = lmsg->msg_control;
    dmsg.msg_controllen = (socklen_t)lmsg->msg_controllen;

    struct sockaddr_storage ss;
    if (lmsg->msg_name && lmsg->msg_namelen > 0) {
        dmsg.msg_name = &ss;
        dmsg.msg_namelen = sizeof(ss);
    }

    ssize_t ret = recvmsg(fd, &dmsg, flags);
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
    lmsg->msg_controllen = dmsg.msg_controllen;
    lmsg->msg_flags = dmsg.msg_flags;

    return ret;
}

int64_t handle_setsockopt(int fd, int level, int optname, const void *optval, socklen_t optlen) {
    int dlevel, doptname;
    translate_linux_to_darwin_sockopt(level, optname, &dlevel, &doptname);
    int ret = setsockopt(fd, dlevel, doptname, optval, optlen);
    if (ret < 0) {
        /* Benign IP metadata options that shouldn't fail applications if unsupported */
        if (level == 0 && (optname == 12 || optname == 13 || optname == 8 || optname == 11 || optname == 7 || optname == 6 || optname == 4)) {
            return 0;
        }
        return darwin_to_linux_errno(errno);
    }
    return 0;
}

int64_t handle_getsockopt(int fd, int level, int optname, void *optval, socklen_t *optlen) {
    int dlevel, doptname;
    translate_linux_to_darwin_sockopt(level, optname, &dlevel, &doptname);
    int ret = getsockopt(fd, dlevel, doptname, optval, optlen);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_shutdown(int fd, int how) {
    int ret = shutdown(fd, how);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

/* Timers & Itimers */
int64_t handle_setitimer(int which, const void *new_val, void *old_val) {
    int ret = setitimer(which, (const struct itimerval *)new_val, (struct itimerval *)old_val);
    if (ret < 0) return darwin_to_linux_errno(errno);
    return 0;
}

int64_t handle_getitimer(int which, void *curr_val) {
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
    (void)flags;
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

