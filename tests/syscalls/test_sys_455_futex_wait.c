#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    volatile uint32_t val = 10;

    // Case A: Happy Path (futex_wait returns -EAGAIN when value mismatches)
    int ret = sys_futex_wait(&val, 0xffffffff, 999, NULL);
    TEST_ASSERT(ret == -EAGAIN, "futex_wait with mismatched value must return -EAGAIN");

    // Case B: Boundary / Edge Cases (Immediate timeout with matching value)
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000; /* 1 microsecond timeout */
    ret = sys_futex_wait(&val, 0xffffffff, 10, &ts);
    TEST_ASSERT(ret == -ETIMEDOUT || ret == 0, "futex_wait with timeout failed");

    // Case C: Error / Errno Handling (NULL uaddr -> -EFAULT)
    ret = sys_futex_wait(NULL, 0xffffffff, 10, NULL);
    TEST_ASSERT(ret == -EFAULT, "futex_wait(NULL) must return -EFAULT");

    TEST_PASS("test_sys_455_futex_wait passed!");
    return 0;
}
