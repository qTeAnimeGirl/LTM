#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    volatile uint32_t val1 = 42;
    volatile uint32_t val2 = 100;

    struct futex_waitv waiters[2];
    waiters[0].val = 42;
    waiters[0].uaddr = (uint64_t)&val1;
    waiters[0].flags = 0;
    waiters[0].__reserved = 0;

    waiters[1].val = 100;
    waiters[1].uaddr = (uint64_t)&val2;
    waiters[1].flags = 0;
    waiters[1].__reserved = 0;

    // Case A: Happy Path (Both waiters match expected values)
    int ret = sys_futex_waitv(waiters, 2, 0, NULL, 0);
    TEST_ASSERT(ret == 0, "futex_waitv matching values failed");

    // Case B: Boundary / Edge Cases (Single waiter)
    ret = sys_futex_waitv(waiters, 1, 0, NULL, 0);
    TEST_ASSERT(ret == 0, "futex_waitv single waiter failed");

    // Mismatched value -> -EAGAIN
    waiters[0].val = 999;
    ret = sys_futex_waitv(waiters, 1, 0, NULL, 0);
    TEST_ASSERT(ret == -EAGAIN, "futex_waitv mismatched value must return -EAGAIN");
    waiters[0].val = 42;

    // Case C: Error / Errno Handling (NULL waiters -> -EFAULT, nr_futexes=0 -> -EINVAL, bad flags -> -EINVAL)
    ret = sys_futex_waitv(NULL, 1, 0, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "futex_waitv(NULL) must return -EFAULT");

    ret = sys_futex_waitv(waiters, 0, 0, NULL, 0);
    TEST_ASSERT(ret == -EINVAL, "futex_waitv(nr=0) must return -EINVAL");

    ret = sys_futex_waitv(waiters, 1, 999, NULL, 0);
    TEST_ASSERT(ret == -EINVAL, "futex_waitv(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_449_futex_waitv passed!");
    return 0;
}
