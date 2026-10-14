#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_014.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);
    sys_close(fd);

    sys_setxattr(path, "user.rm1", "val", 3, 0);

    // Case A: Happy Path
    int ret = sys_removexattr(path, "user.rm1");
    TEST_ASSERT(ret == 0, "removexattr failed");

    // Case B: Boundary / Edge Cases (removing already removed attribute returns -ENODATA)
    ret = sys_removexattr(path, "user.rm1");
    TEST_ASSERT(ret == -ENODATA, "removexattr on deleted attribute must return -ENODATA");

    // Case C: Error / Errno Translation
    ret = sys_removexattr(NULL, "user.rm1");
    TEST_ASSERT(ret == -EFAULT, "removexattr(NULL path) must return -EFAULT");
    ret = sys_removexattr(path, NULL);
    TEST_ASSERT(ret == -EFAULT, "removexattr(NULL name) must return -EFAULT");
    ret = sys_removexattr("nonexistent_removexattr_xyz", "user.rm1");
    TEST_ASSERT(ret == -ENOENT, "removexattr(nonexistent) must return -ENOENT");

    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_014_removexattr passed!");
    return 0;
}
