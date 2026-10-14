#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt umount inside container -> EPERM)
    int ret = sys_umount2("/mnt", 0);
    TEST_ASSERT(ret == -EPERM, "umount2(/mnt) must return -EPERM");

    // Case B: Boundary / Edge Cases (umount2 with flags MNT_FORCE=1, MNT_DETACH=2)
    ret = sys_umount2("/proc", 1 | 2);
    TEST_ASSERT(ret == -EPERM, "umount2(/proc, flags) must return -EPERM");

    // Case C: Error / Errno Handling (NULL target -> EFAULT)
    ret = sys_umount2(NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "umount2(NULL) must return -EFAULT");

    TEST_PASS("test_sys_039_umount2 passed!");
    return 0;
}
