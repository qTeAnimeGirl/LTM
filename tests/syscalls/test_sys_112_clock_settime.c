#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Valid real-time clock setting)
    struct timespec ts;
    ts.tv_sec = 1700000000;
    ts.tv_nsec = 500000;
    int ret = sys_clock_settime(0 /* CLOCK_REALTIME */, &ts);
    TEST_ASSERT(ret == 0, "clock_settime(CLOCK_REALTIME) failed");

    // Case B: Boundary / Edge Cases (Max nanoseconds boundary)
    ts.tv_nsec = 999999999;
    ret = sys_clock_settime(0, &ts);
    TEST_ASSERT(ret == 0, "clock_settime with max nsec failed");

    // Case C: Error / Errno Handling (NULL pointer & invalid nsec & invalid clock)
    ret = sys_clock_settime(0, NULL);
    TEST_ASSERT(ret == -EFAULT, "clock_settime(NULL) must return -EFAULT");

    ts.tv_nsec = 1000000000;
    ret = sys_clock_settime(0, &ts);
    TEST_ASSERT(ret == -EINVAL, "clock_settime(nsec >= 1e9) must return -EINVAL");

    ts.tv_nsec = 0;
    ret = sys_clock_settime(999, &ts);
    TEST_ASSERT(ret == -EINVAL, "clock_settime(invalid_clock) must return -EINVAL");

    TEST_PASS("test_sys_112_clock_settime passed!");
    return 0;
}
