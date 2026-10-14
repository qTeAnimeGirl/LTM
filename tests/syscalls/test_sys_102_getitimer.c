#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (query ITIMER_REAL = 0)
    struct linux_itimerval itv;
    int ret = sys_getitimer(0 /* ITIMER_REAL */, &itv);
    TEST_ASSERT(ret == 0, "getitimer(ITIMER_REAL) failed");

    // Case B: Boundary / Edge Cases (query ITIMER_VIRTUAL = 1)
    ret = sys_getitimer(1 /* ITIMER_VIRTUAL */, &itv);
    TEST_ASSERT(ret == 0, "getitimer(ITIMER_VIRTUAL) failed");

    // Case C: Error / Errno Handling
    ret = sys_getitimer(999, &itv);
    TEST_ASSERT(ret == -EINVAL, "getitimer(invalid_which) must return -EINVAL");
    ret = sys_getitimer(0, NULL);
    TEST_ASSERT(ret == -EFAULT, "getitimer(NULL) must return -EFAULT");

    TEST_PASS("test_sys_102_getitimer passed!");
    return 0;
}
