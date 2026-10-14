#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (current process nice = 0)
    int ret = sys_setpriority(0 /* PRIO_PROCESS */, 0, 0);
    TEST_ASSERT(ret == 0, "setpriority(PRIO_PROCESS, 0, 0) failed");

    // Case B: Boundary / Edge Cases (explicit pid)
    ret = sys_setpriority(0, sys_getpid(), 5);
    TEST_ASSERT(ret == 0, "setpriority with explicit pid failed");

    // Case C: Error / Errno Translation
    ret = sys_setpriority(-1, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "setpriority(which=-1) must return -EINVAL");
    ret = sys_setpriority(0, 999999, 0);
    TEST_ASSERT(ret == -ESRCH, "setpriority(nonexistent pid) must return -ESRCH");

    TEST_PASS("test_sys_140_setpriority passed!");
    return 0;
}
