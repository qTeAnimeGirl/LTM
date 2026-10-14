#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_sched_param param;
    param.sched_priority = 999;

    // Case A: Happy Path
    int ret = sys_sched_getparam(0, &param);
    TEST_ASSERT(ret == 0, "sched_getparam(0) failed");
    TEST_ASSERT(param.sched_priority == 0, "sched_priority should be 0");

    // Case B: Boundary / Edge Cases (explicit pid)
    ret = sys_sched_getparam(sys_getpid(), &param);
    TEST_ASSERT(ret == 0, "sched_getparam(getpid) failed");

    // Case C: Error / Errno Translation
    ret = sys_sched_getparam(0, NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_getparam(NULL) must return -EFAULT");
    ret = sys_sched_getparam(-1, &param);
    TEST_ASSERT(ret == -EINVAL, "sched_getparam(pid < 0) must return -EINVAL");
    ret = sys_sched_getparam(999999, &param);
    TEST_ASSERT(ret == -ESRCH, "sched_getparam(nonexistent) must return -ESRCH");

    TEST_PASS("test_sys_121_sched_getparam passed!");
    return 0;
}
