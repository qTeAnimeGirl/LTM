#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (fsmount returns -EPERM for sandbox security)
    int ret = sys_fsmount(1, 0, 0);
    TEST_ASSERT(ret == -EPERM, "fsmount must return -EPERM in sandbox");

    // Case B: Boundary / Edge Cases (Nonzero flags)
    ret = sys_fsmount(2, 1, 0);
    TEST_ASSERT(ret == -EPERM, "fsmount(flags=1) must return -EPERM");

    // Case C: Error / Errno Handling (Negative fd -> -EBADF)
    ret = sys_fsmount(-1, 0, 0);
    TEST_ASSERT(ret == -EBADF, "fsmount(-1) must return -EBADF");

    TEST_PASS("test_sys_432_fsmount passed!");
    return 0;
}
