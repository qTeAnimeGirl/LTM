#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    unsigned long mask = 0;

    // Case A: Happy Path
    int ret = sys_sched_getaffinity(0, sizeof(mask), &mask);
    TEST_ASSERT(ret >= 0, "sched_getaffinity failed");
    TEST_ASSERT(mask != 0, "mask != 0");

    // Case B: Boundary / Edge Cases
    unsigned long big_mask[16];
    ret = sys_sched_getaffinity(0, sizeof(big_mask), big_mask);
    TEST_ASSERT(ret >= 0, "sched_getaffinity(big_mask) failed");

    // Case C: Error / Errno Handling
    ret = sys_sched_getaffinity(0, 0, &mask);
    TEST_ASSERT(ret == -EINVAL, "sched_getaffinity(len=0) must return -EINVAL");
    ret = sys_sched_getaffinity(0, sizeof(mask), NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_getaffinity(NULL) must return -EFAULT");
    ret = sys_sched_getaffinity(-9999, sizeof(mask), &mask);
    TEST_ASSERT(ret == -ESRCH || ret == -EINVAL, "sched_getaffinity(bad_pid) must return -ESRCH");

    TEST_PASS("test_sys_123_sched_getaffinity passed!");
    return 0;
}
