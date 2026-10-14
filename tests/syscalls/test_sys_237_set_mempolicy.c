#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Set MPOL_DEFAULT = 0)
    int ret = sys_set_mempolicy(0 /* MPOL_DEFAULT */, NULL, 0);
    TEST_ASSERT(ret == 0, "set_mempolicy(MPOL_DEFAULT) failed");

    // Case B: Boundary / Edge Cases (Set MPOL_BIND with nodemask)
    unsigned long nmask = 1;
    ret = sys_set_mempolicy(2 /* MPOL_BIND */, &nmask, 64);
    TEST_ASSERT(ret == 0, "set_mempolicy(MPOL_BIND) failed");

    // Case C: Error / Errno Handling (Invalid mode -> -EINVAL, MPOL_BIND with NULL mask -> -EINVAL)
    ret = sys_set_mempolicy(999, NULL, 0);
    TEST_ASSERT(ret == -EINVAL, "set_mempolicy(invalid mode) must return -EINVAL");

    ret = sys_set_mempolicy(2 /* MPOL_BIND */, NULL, 64);
    TEST_ASSERT(ret == -EINVAL, "set_mempolicy(MPOL_BIND, NULL mask) must return -EINVAL");

    TEST_PASS("test_sys_237_set_mempolicy passed!");
    return 0;
}
