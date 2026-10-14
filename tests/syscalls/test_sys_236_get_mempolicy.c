#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int mode = -1;
    unsigned long nmask = 0;

    // Case A: Happy Path (Query default policy mode)
    int ret = sys_get_mempolicy(&mode, NULL, 0, NULL, 0);
    TEST_ASSERT(ret == 0, "get_mempolicy failed");
    TEST_ASSERT(mode == 0 /* MPOL_DEFAULT */, "get_mempolicy returned non-default mode");

    // Case B: Boundary / Edge Cases (Query with nodemask)
    ret = sys_get_mempolicy(&mode, &nmask, 64, NULL, 0);
    TEST_ASSERT(ret == 0, "get_mempolicy with nodemask failed");

    // Case C: Error / Errno Handling (Invalid flags -> -EINVAL)
    ret = sys_get_mempolicy(&mode, &nmask, 64, NULL, 0x1000 /* invalid flags */);
    TEST_ASSERT(ret == -EINVAL, "get_mempolicy(invalid flags) must return -EINVAL");

    TEST_PASS("test_sys_236_get_mempolicy passed!");
    return 0;
}
