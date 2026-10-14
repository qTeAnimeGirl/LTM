#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_timespec res;
    ltm_memset(&res, 0, sizeof(res));

    // Case A: Happy Path
    int ret = sys_clock_getres(0 /* CLOCK_REALTIME */, &res);
    TEST_ASSERT(ret == 0, "clock_getres(CLOCK_REALTIME) failed");
    TEST_ASSERT(res.tv_nsec > 0, "resolution nsec must be > 0");

    // Case B: Boundary / Edge Cases (NULL buffer is allowed to check clock validity)
    ret = sys_clock_getres(1 /* CLOCK_MONOTONIC */, NULL);
    TEST_ASSERT(ret == 0, "clock_getres(CLOCK_MONOTONIC, NULL) failed");

    // Case C: Error / Errno Translation
    ret = sys_clock_getres(-1, &res);
    TEST_ASSERT(ret == -EINVAL, "clock_getres(-1) must return -EINVAL");
    ret = sys_clock_getres(999, &res);
    TEST_ASSERT(ret == -EINVAL, "clock_getres(999) must return -EINVAL");

    TEST_PASS("test_sys_114_clock_getres passed!");
    return 0;
}
