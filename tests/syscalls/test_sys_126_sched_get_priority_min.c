#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (SCHED_FIFO = 1)
    int ret = sys_sched_get_priority_min(1);
    TEST_ASSERT(ret == 1, "sched_get_priority_min(SCHED_FIFO) must return 1");

    // Case B: Boundary / Edge Cases (SCHED_OTHER = 0)
    ret = sys_sched_get_priority_min(0);
    TEST_ASSERT(ret == 0, "sched_get_priority_min(SCHED_OTHER) must return 0");

    // Case C: Error / Errno Translation
    ret = sys_sched_get_priority_min(-1);
    TEST_ASSERT(ret == -EINVAL, "sched_get_priority_min(-1) must return -EINVAL");
    ret = sys_sched_get_priority_min(999);
    TEST_ASSERT(ret == -EINVAL, "sched_get_priority_min(999) must return -EINVAL");

    TEST_PASS("test_sys_126_sched_get_priority_min passed!");
    return 0;
}
