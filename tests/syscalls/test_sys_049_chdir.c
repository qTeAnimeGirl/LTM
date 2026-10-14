#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_chdir("/");
    TEST_ASSERT(ret == 0, "chdir('/') failed");

    // Case B: Boundary / Edge Cases
    ret = sys_chdir(".");
    TEST_ASSERT(ret == 0, "chdir('.') failed");

    // Case C: Error / Errno Handling
    ret = sys_chdir(NULL);
    TEST_ASSERT(ret == -EFAULT || ret == -EINVAL, "chdir(NULL) must return -EFAULT or -EINVAL");
    ret = sys_chdir("/nonexistent_chdir_path_xyz");
    TEST_ASSERT(ret == -ENOENT, "chdir(nonexistent) must return -ENOENT");

    TEST_PASS("test_sys_049_chdir passed!");
    return 0;
}
