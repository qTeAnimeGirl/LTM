#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Set best-effort priority on calling process)
    int ioprio = (IOPRIO_CLASS_BE << 13) | 4;
    int ret = sys_ioprio_set(IOPRIO_WHO_PROCESS, 0, ioprio);
    TEST_ASSERT(ret == 0, "ioprio_set(WHO_PROCESS, 0) failed");

    // Case B: Boundary / Edge Cases (Set idle priority class)
    ioprio = (IOPRIO_CLASS_IDLE << 13) | 0;
    ret = sys_ioprio_set(IOPRIO_WHO_PROCESS, 0, ioprio);
    TEST_ASSERT(ret == 0, "ioprio_set(WHO_PROCESS, IDLE) failed");

    // Case C: Error / Errno Translation
    ret = sys_ioprio_set(999, 0, ioprio);
    TEST_ASSERT(ret == -EINVAL, "ioprio_set(invalid which) must return -EINVAL");

    ret = sys_ioprio_set(IOPRIO_WHO_PROCESS, -1, ioprio);
    TEST_ASSERT(ret == -ESRCH, "ioprio_set(negative who) must return -ESRCH");

    ret = sys_ioprio_set(IOPRIO_WHO_PROCESS, 9999999, ioprio);
    TEST_ASSERT(ret == -ESRCH, "ioprio_set(nonexistent pid) must return -ESRCH");

    ret = sys_ioprio_set(IOPRIO_WHO_PROCESS, 0, (7 << 13) | 0);
    TEST_ASSERT(ret == -EINVAL, "ioprio_set(invalid class) must return -EINVAL");

    TEST_PASS("test_sys_030_ioprio_set passed!");
    return 0;
}
