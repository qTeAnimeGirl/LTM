typedef unsigned long uint64_t;
typedef long int64_t;

static inline int64_t ltm_syscall(int64_t num, int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5) {
    register int64_t x8 __asm__("x8") = num;
    register int64_t x0 __asm__("x0") = a0;
    register int64_t x1 __asm__("x1") = a1;
    register int64_t x2 __asm__("x2") = a2;
    register int64_t x3 __asm__("x3") = a3;
    register int64_t x4 __asm__("x4") = a4;
    register int64_t x5 __asm__("x5") = a5;
    __asm__ volatile (
        "svc #0\n"
        : "+r"(x0)
        : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
        : "memory"
    );
    return x0;
}

static void print_str(const char *s) {
    uint64_t len = 0;
    while (s[len]) len++;
    ltm_syscall(64, 1, (int64_t)s, len, 0, 0, 0); // sys_write
}

void _start(void) {
    print_str("[GUEST] Direct C syscall execution starting...\n");
    int64_t pid = ltm_syscall(172, 0, 0, 0, 0, 0, 0); // sys_getpid
    if (pid > 0) {
        print_str("[GUEST] sys_getpid succeeded!\n");
    }
    ltm_syscall(93, 0, 0, 0, 0, 0, 0); // sys_exit(0)
}
