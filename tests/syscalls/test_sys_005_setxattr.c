#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_005.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_setxattr(path, "user.ltm_test", "hello_xattr", 11, 0);
    TEST_ASSERT(ret == 0, "setxattr happy path failed");

    // Case B: Boundary / Edge Cases (replace existing attribute)
    ret = sys_setxattr(path, "user.ltm_test", "new_val", 7, XATTR_REPLACE);
    TEST_ASSERT(ret == 0, "setxattr XATTR_REPLACE failed");

    // Case C: Error / Errno Translation
    ret = sys_setxattr(NULL, "user.ltm_test", "val", 3, 0);
    TEST_ASSERT(ret == -EFAULT, "setxattr(NULL path) must return -EFAULT");

    ret = sys_setxattr(path, NULL, "val", 3, 0);
    TEST_ASSERT(ret == -EFAULT, "setxattr(NULL name) must return -EFAULT");

    ret = sys_setxattr("nonexistent_file_xattr_xyz", "user.ltm_test", "val", 3, 0);
    TEST_ASSERT(ret == -ENOENT, "setxattr(nonexistent) must return -ENOENT");

    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_005_setxattr passed!");
    return 0;
}
