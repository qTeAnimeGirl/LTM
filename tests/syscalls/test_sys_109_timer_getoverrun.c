#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int timerid = -1;
    int ret = sys_timer_create(0, NULL, &timerid);
    TEST_ASSERT(ret == 0, "timer_create failed");

    // Case A: Happy Path (Query overrun count >= 0)
    int ov = sys_timer_getoverrun(timerid);
    TEST_ASSERT(ov >= 0, "timer_getoverrun failed");

    // Case B: Boundary / Edge Cases (Query overrun on freshly armed timer)
    struct itimerspec new_val;
    new_val.it_interval.tv_sec = 0;
    new_val.it_interval.tv_nsec = 500000000;
    new_val.it_value.tv_sec = 1;
    new_val.it_value.tv_nsec = 0;
    ret = sys_timer_settime(timerid, 0, &new_val, NULL);
    TEST_ASSERT(ret == 0, "timer_settime failed");

    ov = sys_timer_getoverrun(timerid);
    TEST_ASSERT(ov >= 0, "timer_getoverrun must be non-negative");

    // Case C: Error / Errno Translation
    ret = sys_timer_getoverrun(-1);
    TEST_ASSERT(ret == -EINVAL, "timer_getoverrun(bad id) must return -EINVAL");

    sys_timer_delete(timerid);

    TEST_PASS("test_sys_109_timer_getoverrun passed!");
    return 0;
}
