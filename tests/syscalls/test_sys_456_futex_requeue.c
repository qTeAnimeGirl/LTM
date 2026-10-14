#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    volatile uint32_t uaddr1 = 1;
    volatile uint32_t uaddr2 = 2;

    // Case A: Happy Path (Requeue 0 waiters)
    int ret = sys_futex_requeue(&uaddr1, 0xffffffff, 1, &uaddr2, 1);
    TEST_ASSERT(ret >= 0, "futex_requeue failed");

    // Case B: Boundary / Edge Cases (Requeue with large counts)
    ret = sys_futex_requeue(&uaddr1, 0xffffffff, 0, &uaddr2, 10);
    TEST_ASSERT(ret >= 0, "futex_requeue(0, 10) failed");

    // Case C: Error / Errno Handling (NULL uaddr -> -EFAULT, negative counts -> -EINVAL)
    ret = sys_futex_requeue(NULL, 0xffffffff, 1, &uaddr2, 1);
    TEST_ASSERT(ret == -EFAULT, "futex_requeue(NULL uaddr1) must return -EFAULT");

    ret = sys_futex_requeue(&uaddr1, 0xffffffff, 1, NULL, 1);
    TEST_ASSERT(ret == -EFAULT, "futex_requeue(NULL uaddr2) must return -EFAULT");

    ret = sys_futex_requeue(&uaddr1, 0xffffffff, -1, &uaddr2, 1);
    TEST_ASSERT(ret == -EINVAL, "futex_requeue(bad nr) must return -EINVAL");

    TEST_PASS("test_sys_456_futex_requeue passed!");
    return 0;
}
