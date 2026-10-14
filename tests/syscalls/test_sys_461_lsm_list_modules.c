#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    uint64_t ids[4];
    size_t size = sizeof(ids);

    // Case A: Happy Path (Query active LSM modules returns 0 modules)
    int ret = sys_lsm_list_modules(ids, &size, 0);
    TEST_ASSERT(ret == 0, "lsm_list_modules failed");
    TEST_ASSERT(size == 0, "active modules count must be 0");

    // Case B: Boundary / Edge Cases (Size query with NULL ids)
    size = 0;
    ret = sys_lsm_list_modules(NULL, &size, 0);
    TEST_ASSERT(ret == 0, "lsm_list_modules(NULL ids) failed");

    // Case C: Error / Errno Handling (NULL size -> -EFAULT, bad flags -> -EINVAL)
    ret = sys_lsm_list_modules(ids, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "lsm_list_modules(NULL size) must return -EFAULT");

    ret = sys_lsm_list_modules(ids, &size, 999);
    TEST_ASSERT(ret == -EINVAL, "lsm_list_modules(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_461_lsm_list_modules passed!");
    return 0;
}
