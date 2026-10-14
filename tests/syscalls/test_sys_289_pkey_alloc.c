#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (pkey_alloc with valid 0 flags returns -ENOSPC on standard ARM64 architecture)
    int ret = sys_pkey_alloc(0, 0);
    TEST_ASSERT(ret == -ENOSPC, "pkey_alloc(0, 0) must return -ENOSPC on Apple Silicon");

    // Case B: Boundary / Edge Cases (pkey_alloc with access rights flags)
    ret = sys_pkey_alloc(0, 1 /* PKEY_DISABLE_ACCESS */);
    TEST_ASSERT(ret == -ENOSPC, "pkey_alloc(0, 1) must return -ENOSPC");

    // Case C: Error / Errno Handling (invalid nonzero flags -> -EINVAL)
    ret = sys_pkey_alloc(1, 0);
    TEST_ASSERT(ret == -EINVAL, "pkey_alloc with invalid flags must return -EINVAL");

    TEST_PASS("test_sys_289_pkey_alloc passed!");
    return 0;
}
