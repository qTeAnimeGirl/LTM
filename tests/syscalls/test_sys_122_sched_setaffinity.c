#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    unsigned long mask = 0;
    sys_sched_getaffinity(0, sizeof(mask), &mask);

    // Case A: Happy Path
    int ret = sys_sched_setaffinity(0, sizeof(mask), &mask);
    TEST_ASSERT(ret == 0, "sched_setaffinity failed");

    // Case B: Boundary / Edge Cases
    unsigned long mask2 = 1;
    ret = sys_sched_setaffinity(0, sizeof(mask2), &mask2);
    TEST_ASSERT(ret == 0, "sched_setaffinity(mask=1) failed");

    // Case C: Error / Errno Handling
    ret = sys_sched_setaffinity(0, 0, &mask);
    TEST_ASSERT(ret == -EINVAL, "sched_setaffinity(len=0) must return -EINVAL");
    ret = sys_sched_setaffinity(0, sizeof(mask), NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_setaffinity(NULL) must return -EFAULT");

    TEST_PASS("test_sys_122_sched_setaffinity passed!");
    return 0;
}
