#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    char buf[512];
    int ret = sys_readlinkat(AT_FDCWD, "/proc/self/exe", buf, sizeof(buf));
    TEST_ASSERT(ret > 0, "readlinkat('/proc/self/exe') failed");

    // Case B: Boundary / Edge Cases (small buffer)
    char small_buf[4];
    ret = sys_readlinkat(AT_FDCWD, "/proc/self/exe", small_buf, sizeof(small_buf));
    TEST_ASSERT(ret > 0, "readlinkat with small buf failed");

    // Case C: Error / Errno Handling
    ret = sys_readlinkat(AT_FDCWD, NULL, buf, sizeof(buf));
    TEST_ASSERT(ret == -EFAULT, "readlinkat(NULL) must return -EFAULT");
    ret = sys_readlinkat(AT_FDCWD, "/nonexistent_symlink_xyz", buf, sizeof(buf));
    TEST_ASSERT(ret == -ENOENT, "readlinkat(nonexistent) must return -ENOENT");

    TEST_PASS("test_sys_078_readlinkat passed!");
    return 0;
}
