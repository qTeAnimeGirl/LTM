#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_sched_param param;
    param.sched_priority = 0;

    // Case A: Happy Path (SCHED_OTHER = 0)
    int ret = sys_sched_setscheduler(0, 0, &param);
    TEST_ASSERT(ret == 0, "sched_setscheduler(0, SCHED_OTHER) failed");

    // Case B: Boundary / Edge Cases (SCHED_RR = 2)
    ret = sys_sched_setscheduler(sys_getpid(), 2, &param);
    TEST_ASSERT(ret == 0, "sched_setscheduler(SCHED_RR) failed");

    // Case C: Error / Errno Translation
    ret = sys_sched_setscheduler(0, 999, &param);
    TEST_ASSERT(ret == -EINVAL, "sched_setscheduler(invalid policy) must return -EINVAL");
    ret = sys_sched_setscheduler(0, 0, NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_setscheduler(NULL) must return -EFAULT");
    ret = sys_sched_setscheduler(999999, 0, &param);
    TEST_ASSERT(ret == -ESRCH, "sched_setscheduler(nonexistent) must return -ESRCH");

    TEST_PASS("test_sys_119_sched_setscheduler passed!");
    return 0;
}
