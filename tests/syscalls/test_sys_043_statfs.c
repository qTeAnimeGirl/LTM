#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_statfs sfs;
    ltm_memset(&sfs, 0, sizeof(sfs));

    // Case A: Happy Path
    int ret = sys_statfs(".", &sfs);
    TEST_ASSERT(ret == 0, "statfs('.') failed");
    TEST_ASSERT(sfs.f_bsize > 0, "statfs.f_bsize > 0");
    TEST_ASSERT(sfs.f_blocks > 0, "statfs.f_blocks > 0");

    // Case B: Boundary / Edge Cases
    ret = sys_statfs("/", &sfs);
    TEST_ASSERT(ret == 0, "statfs('/') failed");

    // Case C: Error / Errno Handling
    ret = sys_statfs(NULL, &sfs);
    TEST_ASSERT(ret == -EFAULT, "statfs(NULL) must return -EFAULT");
    ret = sys_statfs("/nonexistent_statfs_xyz", &sfs);
    TEST_ASSERT(ret == -ENOENT, "statfs(nonexistent) must return -ENOENT");

    TEST_PASS("test_sys_043_statfs passed!");
    return 0;
}
