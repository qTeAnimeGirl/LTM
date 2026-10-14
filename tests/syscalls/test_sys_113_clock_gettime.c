#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (CLOCK_REALTIME = 0)
    struct linux_timespec ts;
    int ret = sys_clock_gettime(0 /* CLOCK_REALTIME */, &ts);
    TEST_ASSERT(ret == 0, "clock_gettime(REALTIME) failed");
    TEST_ASSERT(ts.tv_sec > 1000000000L, "clock_gettime seconds sanity check");

    // Case B: Boundary / Edge Cases (CLOCK_MONOTONIC = 1)
    ret = sys_clock_gettime(1 /* CLOCK_MONOTONIC */, &ts);
    TEST_ASSERT(ret == 0, "clock_gettime(MONOTONIC) failed");
    TEST_ASSERT(ts.tv_sec >= 0, "monotonic seconds sanity check");

    // Case C: Error / Errno Handling
    ret = sys_clock_gettime(0, NULL);
    TEST_ASSERT(ret == -EFAULT, "clock_gettime(NULL) must return -EFAULT");
    ret = sys_clock_gettime(9999, &ts);
    TEST_ASSERT(ret == -EINVAL, "clock_gettime(invalid_clock) must return -EINVAL");

    TEST_PASS("test_sys_113_clock_gettime passed!");
    return 0;
}
