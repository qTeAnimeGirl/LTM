#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    volatile uint32_t val = 1;

    // Case A: Happy Path (Wake up to 1 waiter on variable with 0 active waiters)
    int ret = sys_futex_wake(&val, 0xffffffff, 1);
    TEST_ASSERT(ret >= 0, "futex_wake failed");

    // Case B: Boundary / Edge Cases (Wake up to INT_MAX waiters)
    ret = sys_futex_wake(&val, 0xffffffff, 0x7fffffff);
    TEST_ASSERT(ret >= 0, "futex_wake(INT_MAX) failed");

    // Case C: Error / Errno Handling (NULL uaddr -> -EFAULT, negative nr_wake -> -EINVAL)
    ret = sys_futex_wake(NULL, 0xffffffff, 1);
    TEST_ASSERT(ret == -EFAULT, "futex_wake(NULL) must return -EFAULT");

    ret = sys_futex_wake(&val, 0xffffffff, -1);
    TEST_ASSERT(ret == -EINVAL, "futex_wake(negative nr) must return -EINVAL");

    TEST_PASS("test_sys_454_futex_wake passed!");
    return 0;
}
