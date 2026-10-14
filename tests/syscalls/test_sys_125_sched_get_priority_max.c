#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (SCHED_FIFO = 1)
    int ret = sys_sched_get_priority_max(1);
    TEST_ASSERT(ret == 99, "sched_get_priority_max(SCHED_FIFO) must return 99");

    // Case B: Boundary / Edge Cases (SCHED_OTHER = 0)
    ret = sys_sched_get_priority_max(0);
    TEST_ASSERT(ret == 0, "sched_get_priority_max(SCHED_OTHER) must return 0");

    // Case C: Error / Errno Translation
    ret = sys_sched_get_priority_max(-1);
    TEST_ASSERT(ret == -EINVAL, "sched_get_priority_max(-1) must return -EINVAL");
    ret = sys_sched_get_priority_max(999);
    TEST_ASSERT(ret == -EINVAL, "sched_get_priority_max(999) must return -EINVAL");

    TEST_PASS("test_sys_125_sched_get_priority_max passed!");
    return 0;
}
