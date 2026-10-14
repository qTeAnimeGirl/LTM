#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_sched_getscheduler(0);
    TEST_ASSERT(ret == 0, "sched_getscheduler(0) must return SCHED_OTHER (0)");

    // Case B: Boundary / Edge Cases (explicit pid)
    ret = sys_sched_getscheduler(sys_getpid());
    TEST_ASSERT(ret == 0, "sched_getscheduler(getpid) failed");

    // Case C: Error / Errno Translation
    ret = sys_sched_getscheduler(-1);
    TEST_ASSERT(ret == -EINVAL, "sched_getscheduler(pid < 0) must return -EINVAL");
    ret = sys_sched_getscheduler(999999);
    TEST_ASSERT(ret == -ESRCH, "sched_getscheduler(nonexistent) must return -ESRCH");

    TEST_PASS("test_sys_120_sched_getscheduler passed!");
    return 0;
}
