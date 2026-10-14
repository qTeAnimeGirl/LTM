#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Get ioprio for current process)
    int ret = sys_ioprio_get(IOPRIO_WHO_PROCESS, 0);
    TEST_ASSERT(ret >= 0, "ioprio_get(WHO_PROCESS, 0) failed");

    // Case B: Boundary / Edge Cases (Get ioprio with getpid())
    int pid = sys_getpid();
    ret = sys_ioprio_get(IOPRIO_WHO_PROCESS, pid);
    TEST_ASSERT(ret >= 0, "ioprio_get(WHO_PROCESS, getpid()) failed");

    // Case C: Error / Errno Translation
    ret = sys_ioprio_get(999, 0);
    TEST_ASSERT(ret == -EINVAL, "ioprio_get(invalid which) must return -EINVAL");

    ret = sys_ioprio_get(IOPRIO_WHO_PROCESS, -1);
    TEST_ASSERT(ret == -ESRCH, "ioprio_get(negative who) must return -ESRCH");

    ret = sys_ioprio_get(IOPRIO_WHO_PROCESS, 9999999);
    TEST_ASSERT(ret == -ESRCH, "ioprio_get(nonexistent pid) must return -ESRCH");

    TEST_PASS("test_sys_031_ioprio_get passed!");
    return 0;
}
