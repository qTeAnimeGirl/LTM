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
#define SYS_openat      56
#define SYS_close       57
#define SYS_lseek       62
#define SYS_read        63
#define SYS_write       64
#define SYS_newfstatat  79
#define SYS_exit        93
#define SYS_exit_group  94
#define SYS_uname       160
#define SYS_getpid      172
#define SYS_getuid      174
#define SYS_getgid      176
#define SYS_brk         214
#define SYS_munmap      215
#define SYS_mmap        222

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
#define ENOMEM          12
#define EACCES          13
#define EFAULT          14
#define EEXIST          17
#define EINVAL          22

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
