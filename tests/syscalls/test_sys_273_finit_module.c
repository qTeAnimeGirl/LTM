#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt finit_module in container -> -EPERM)
    int ret = sys_finit_module(0, "", 0);
    TEST_ASSERT(ret == -EPERM, "finit_module must return -EPERM");

    // Case B: Boundary / Edge Cases (finit_module with MODULE_INIT_IGNORE_MODVERSIONS = 1)
    ret = sys_finit_module(0, "param=1", 1);
    TEST_ASSERT(ret == -EPERM, "finit_module with flags must return -EPERM");

    // Case C: Error / Errno Handling (Bad fd -> -EBADF)
    ret = sys_finit_module(-1, "", 0);
    TEST_ASSERT(ret == -EBADF, "finit_module(-1) must return -EBADF");

    TEST_PASS("test_sys_273_finit_module passed!");
    return 0;
}
