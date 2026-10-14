#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_chroot(".");
    TEST_ASSERT(ret == 0, "chroot('.') failed");

    // Case B: Boundary / Edge Cases
    ret = sys_chroot("/");
    TEST_ASSERT(ret == 0, "chroot('/') failed");

    // Case C: Error / Errno Handling
    ret = sys_chroot(NULL);
    TEST_ASSERT(ret == -EFAULT, "chroot(NULL) must return -EFAULT");
    ret = sys_chroot("/nonexistent_chroot_path_xyz");
    TEST_ASSERT(ret == -ENOENT, "chroot(nonexistent) must return -ENOENT");

    TEST_PASS("test_sys_051_chroot passed!");
    return 0;
}
