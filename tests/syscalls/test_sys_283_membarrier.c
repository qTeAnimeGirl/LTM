#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Query supported membarrier commands)
    int ret = sys_membarrier(0 /* MEMBARRIER_CMD_QUERY */, 0, 0);
    TEST_ASSERT(ret > 0, "membarrier(CMD_QUERY) failed");
    TEST_ASSERT((ret & (1 << 0)) != 0, "MEMBARRIER_CMD_GLOBAL must be supported");

    // Case B: Boundary / Edge Cases (Execute global and private expedited barriers)
    ret = sys_membarrier(1 /* MEMBARRIER_CMD_GLOBAL */, 0, 0);
    TEST_ASSERT(ret == 0, "membarrier(CMD_GLOBAL) failed");

    ret = sys_membarrier(8 /* MEMBARRIER_CMD_PRIVATE_EXPEDITED */, 0, 0);
    TEST_ASSERT(ret == 0, "membarrier(CMD_PRIVATE_EXPEDITED) failed");

    // Case C: Error / Errno Handling (Invalid command and invalid flags)
    ret = sys_membarrier(999, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "membarrier(invalid cmd) must return -EINVAL");

    ret = sys_membarrier(0, 1 /* flags != 0 */, 0);
    TEST_ASSERT(ret == -EINVAL, "membarrier(flags != 0) must return -EINVAL");

    TEST_PASS("test_sys_283_membarrier passed!");
    return 0;
}
