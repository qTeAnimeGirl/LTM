#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Create CLOCK_REALTIME timer)
    int timerid = -1;
    int ret = sys_timer_create(0 /* CLOCK_REALTIME */, NULL, &timerid);
    TEST_ASSERT(ret == 0, "timer_create(CLOCK_REALTIME) failed");
    TEST_ASSERT(timerid >= 0, "timerid must be >= 0");

    // Case B: Boundary / Edge Cases (Create CLOCK_MONOTONIC & BOOTTIME timers)
    int timerid_mono = -1;
    ret = sys_timer_create(1 /* CLOCK_MONOTONIC */, NULL, &timerid_mono);
    TEST_ASSERT(ret == 0, "timer_create(CLOCK_MONOTONIC) failed");
    TEST_ASSERT(timerid_mono >= 0, "timerid_mono must be >= 0");

    // Case C: Error / Errno Translation
    ret = sys_timer_create(0, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "timer_create(NULL timerid) must return -EFAULT");

    ret = sys_timer_create(999, NULL, &timerid);
    TEST_ASSERT(ret == -EINVAL, "timer_create(invalid clockid) must return -EINVAL");

    sys_timer_delete(timerid);
    sys_timer_delete(timerid_mono);

    TEST_PASS("test_sys_107_timer_create passed!");
    return 0;
}
