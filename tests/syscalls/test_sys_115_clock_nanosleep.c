#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_timespec req;
    struct linux_timespec rem;

    // Case A: Happy Path (1ms sleep)
    req.tv_sec = 0;
    req.tv_nsec = 1000000;
    int ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, &req, &rem);
    TEST_ASSERT(ret == 0, "clock_nanosleep(1ms) failed");

    // Case B: Boundary / Edge Cases (0ns sleep, remain=NULL)
    req.tv_sec = 0;
    req.tv_nsec = 0;
    ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    TEST_ASSERT(ret == 0, "clock_nanosleep(0) failed");

    // Case C: Error / Errno Handling
    ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "clock_nanosleep(NULL) must return -EFAULT");
    req.tv_nsec = 1000000000L;
    ret = sys_clock_nanosleep(CLOCK_MONOTONIC, 0, &req, NULL);
    TEST_ASSERT(ret == -EINVAL, "clock_nanosleep(invalid_nsec) must return -EINVAL");
    req.tv_nsec = 1000;
    ret = sys_clock_nanosleep(9999, 0, &req, NULL);
    TEST_ASSERT(ret == -EINVAL, "clock_nanosleep(invalid_clock) must return -EINVAL");

    TEST_PASS("test_sys_115_clock_nanosleep passed!");
    return 0;
}
