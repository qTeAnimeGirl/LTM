#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_timespec tp;
    ltm_memset(&tp, 0, sizeof(tp));

    // Case A: Happy Path
    int ret = sys_sched_rr_get_interval(0, &tp);
    TEST_ASSERT(ret == 0, "sched_rr_get_interval(0) failed");
    TEST_ASSERT(tp.tv_nsec > 0 || tp.tv_sec > 0, "interval timeslice must be > 0");

    // Case B: Boundary / Edge Cases (explicit pid)
    ret = sys_sched_rr_get_interval(sys_getpid(), &tp);
    TEST_ASSERT(ret == 0, "sched_rr_get_interval(getpid) failed");

    // Case C: Error / Errno Translation
    ret = sys_sched_rr_get_interval(0, NULL);
    TEST_ASSERT(ret == -EFAULT, "sched_rr_get_interval(NULL) must return -EFAULT");
    ret = sys_sched_rr_get_interval(-1, &tp);
    TEST_ASSERT(ret == -EINVAL, "sched_rr_get_interval(pid < 0) must return -EINVAL");
    ret = sys_sched_rr_get_interval(999999, &tp);
    TEST_ASSERT(ret == -ESRCH, "sched_rr_get_interval(nonexistent) must return -ESRCH");

    TEST_PASS("test_sys_127_sched_rr_get_interval passed!");
    return 0;
}
