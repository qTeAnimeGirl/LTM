#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_015.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);
    sys_close(fd);

    sys_lsetxattr(path, "user.lrm1", "val", 3, 0);

    // Case A: Happy Path
    int ret = sys_lremovexattr(path, "user.lrm1");
    TEST_ASSERT(ret == 0, "lremovexattr failed");

    // Case B: Boundary / Edge Cases
    ret = sys_lremovexattr(path, "user.lrm1");
    TEST_ASSERT(ret == -ENODATA, "lremovexattr second time must return -ENODATA");

    // Case C: Error / Errno Translation
    ret = sys_lremovexattr(NULL, "user.lrm1");
    TEST_ASSERT(ret == -EFAULT, "lremovexattr(NULL path) must return -EFAULT");
    ret = sys_lremovexattr("nonexistent_lremovexattr_xyz", "user.lrm1");
    TEST_ASSERT(ret == -ENOENT, "lremovexattr(nonexistent) must return -ENOENT");

    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_015_lremovexattr passed!");
    return 0;
}
