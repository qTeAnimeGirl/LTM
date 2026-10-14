#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_faccessat(AT_FDCWD, "/", 0 /* F_OK */, 0);
    TEST_ASSERT(ret == 0, "faccessat('/') failed");

    // Case B: Boundary / Edge Cases (check execute on /bin/sh if present or /)
    ret = sys_faccessat(AT_FDCWD, "/", 1 /* X_OK */, 0);
    TEST_ASSERT(ret == 0, "faccessat('/', X_OK) failed");

    // Case C: Error / Errno Handling
    ret = sys_faccessat(AT_FDCWD, NULL, 0, 0);
    TEST_ASSERT(ret == -EFAULT, "faccessat(NULL) must return -EFAULT");
    ret = sys_faccessat(AT_FDCWD, "/nonexistent_faccessat_path_xyz", 0, 0);
    TEST_ASSERT(ret == -ENOENT, "faccessat(nonexistent) must return -ENOENT");

    TEST_PASS("test_sys_048_faccessat passed!");
    return 0;
}
