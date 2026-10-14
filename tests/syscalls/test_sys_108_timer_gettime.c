#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int timerid = -1;
    int ret = sys_timer_create(0, NULL, &timerid);
    TEST_ASSERT(ret == 0, "timer_create failed");

    // Case A: Happy Path (Query disarmed timer)
    struct itimerspec its;
    ret = sys_timer_gettime(timerid, &its);
    TEST_ASSERT(ret == 0, "timer_gettime failed");

    // Case B: Boundary / Edge Cases (Arm timer and query)
    struct itimerspec new_val;
    new_val.it_interval.tv_sec = 1;
    new_val.it_interval.tv_nsec = 0;
    new_val.it_value.tv_sec = 2;
    new_val.it_value.tv_nsec = 0;
    ret = sys_timer_settime(timerid, 0, &new_val, NULL);
    TEST_ASSERT(ret == 0, "timer_settime failed");

    ret = sys_timer_gettime(timerid, &its);
    TEST_ASSERT(ret == 0, "timer_gettime failed after settime");
    TEST_ASSERT(its.it_interval.tv_sec == 1, "interval sec mismatch");

    // Case C: Error / Errno Translation
    ret = sys_timer_gettime(timerid, NULL);
    TEST_ASSERT(ret == -EFAULT, "timer_gettime(NULL) must return -EFAULT");

    ret = sys_timer_gettime(-1, &its);
    TEST_ASSERT(ret == -EINVAL, "timer_gettime(bad id) must return -EINVAL");

    sys_timer_delete(timerid);

    TEST_PASS("test_sys_108_timer_gettime passed!");
    return 0;
}
