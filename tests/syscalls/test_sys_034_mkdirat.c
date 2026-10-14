#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *dirname = "tmp_mkdirat_034";
    sys_unlinkat(AT_FDCWD, dirname, AT_REMOVEDIR);

    // Case A: Happy Path
    int ret = sys_mkdirat(AT_FDCWD, dirname, 0755);
    TEST_ASSERT(ret == 0, "mkdirat failed");

    // Case B: Boundary / Edge Cases (already exists)
    ret = sys_mkdirat(AT_FDCWD, dirname, 0755);
    TEST_ASSERT(ret == -EEXIST, "mkdirat(existing) must return -EEXIST");

    // Case C: Error / Errno Handling
    ret = sys_mkdirat(AT_FDCWD, NULL, 0755);
    TEST_ASSERT(ret == -EFAULT, "mkdirat(NULL) must return -EFAULT");
    ret = sys_mkdirat(-999, dirname, 0755);
    TEST_ASSERT(ret == -EBADF, "mkdirat(invalid_dirfd) must return -EBADF");

    sys_unlinkat(AT_FDCWD, dirname, AT_REMOVEDIR);

    TEST_PASS("test_sys_034_mkdirat passed!");
    return 0;
}
