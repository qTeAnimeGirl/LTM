#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (free pkey 0)
    int ret = sys_pkey_free(0);
    TEST_ASSERT(ret == 0, "pkey_free(0) failed");

    // Case B: Boundary / Edge Cases (free higher valid pkey)
    ret = sys_pkey_free(15);
    TEST_ASSERT(ret == 0, "pkey_free(15) failed");

    // Case C: Error / Errno Handling (negative pkey -> -EINVAL)
    ret = sys_pkey_free(-1);
    TEST_ASSERT(ret == -EINVAL, "pkey_free(-1) must return -EINVAL");

    ret = sys_pkey_free(-100);
    TEST_ASSERT(ret == -EINVAL, "pkey_free(-100) must return -EINVAL");

    TEST_PASS("test_sys_290_pkey_free passed!");
    return 0;
}
