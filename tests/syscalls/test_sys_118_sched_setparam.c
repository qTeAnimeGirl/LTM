#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_sched_param param;
    param.sched_priority = 0;

    // Case A: Happy Path (current process)
    int ret = sys_sched_setparam(0, &param);
    TEST_ASSERT(ret == 0, "sched_setparam(0) failed");

    // Case B: Explicit PID
    ret = sys_sched_setparam(sys_getpid(), &param);
    TEST_ASSERT(ret == 0, "sched_setparam(getpid) failed");

    // Case C: Error / Errno Translation
    ret = sys_sched_setparam(0, NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_setparam(NULL) must return -EFAULT");
    ret = sys_sched_setparam(-1, &param);
    TEST_ASSERT(ret == -EINVAL, "sched_setparam(pid < 0) must return -EINVAL");
    ret = sys_sched_setparam(999999, &param);
    TEST_ASSERT(ret == -ESRCH, "sched_setparam(nonexistent) must return -ESRCH");

    TEST_PASS("test_sys_118_sched_setparam passed!");
    return 0;
}
