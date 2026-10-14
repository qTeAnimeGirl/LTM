#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt setns on standard fd -> -EPERM)
    int ret = sys_setns(0, 0);
    TEST_ASSERT(ret == -EPERM, "setns must return -EPERM");

    // Case B: Boundary / Edge Cases (setns with CLONE_NEWNET = 0x40000000)
    ret = sys_setns(0, 0x40000000 /* CLONE_NEWNET */);
    TEST_ASSERT(ret == -EPERM, "setns(CLONE_NEWNET) must return -EPERM");

    // Case C: Error / Errno Handling (Bad fd -> -EBADF, invalid nstype -> -EINVAL)
    ret = sys_setns(-1, 0);
    TEST_ASSERT(ret == -EBADF, "setns(-1) must return -EBADF");

    ret = sys_setns(0, 0x01 /* invalid nstype */);
    TEST_ASSERT(ret == -EINVAL, "setns(invalid nstype) must return -EINVAL");

    TEST_PASS("test_sys_268_setns passed!");
    return 0;
}
