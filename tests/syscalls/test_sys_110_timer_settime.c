#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int timerid = -1;
    int ret = sys_timer_create(0, NULL, &timerid);
    TEST_ASSERT(ret == 0, "timer_create failed");

    // Case A: Happy Path (Arm timer with relative duration)
    struct itimerspec new_val;
    new_val.it_interval.tv_sec = 0;
    new_val.it_interval.tv_nsec = 100000000;
    new_val.it_value.tv_sec = 1;
    new_val.it_value.tv_nsec = 0;

    struct itimerspec old_val;
    ret = sys_timer_settime(timerid, 0, &new_val, &old_val);
    TEST_ASSERT(ret == 0, "timer_settime failed");

    // Case B: Boundary / Edge Cases (Disarm timer with value = 0)
    struct itimerspec disarm;
    disarm.it_interval.tv_sec = 0;
    disarm.it_interval.tv_nsec = 0;
    disarm.it_value.tv_sec = 0;
    disarm.it_value.tv_nsec = 0;
    ret = sys_timer_settime(timerid, 0, &disarm, NULL);
    TEST_ASSERT(ret == 0, "disarming timer failed");

    // Case C: Error / Errno Translation
    ret = sys_timer_settime(timerid, 0, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "timer_settime(NULL new_val) must return -EFAULT");

    struct itimerspec invalid_val = new_val;
    invalid_val.it_value.tv_nsec = 1000000000; // >= 1e9
    ret = sys_timer_settime(timerid, 0, &invalid_val, NULL);
    TEST_ASSERT(ret == -EINVAL, "timer_settime(invalid nsec) must return -EINVAL");

    ret = sys_timer_settime(-1, 0, &new_val, NULL);
    TEST_ASSERT(ret == -EINVAL, "timer_settime(bad id) must return -EINVAL");

    sys_timer_delete(timerid);

    TEST_PASS("test_sys_110_timer_settime passed!");
    return 0;
}
