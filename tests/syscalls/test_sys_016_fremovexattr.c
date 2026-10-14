#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_016.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);

    sys_fsetxattr(fd, "user.frm1", "val", 3, 0);

    // Case A: Happy Path
    int ret = sys_fremovexattr(fd, "user.frm1");
    TEST_ASSERT(ret == 0, "fremovexattr failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fremovexattr(fd, "user.frm1");
    TEST_ASSERT(ret == -ENODATA, "fremovexattr second time must return -ENODATA");

    // Case C: Error / Errno Translation
    ret = sys_fremovexattr(-1, "user.frm1");
    TEST_ASSERT(ret == -EBADF, "fremovexattr(-1) must return -EBADF");
    ret = sys_fremovexattr(fd, NULL);
    TEST_ASSERT(ret == -EFAULT, "fremovexattr(NULL) must return -EFAULT");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_016_fremovexattr passed!");
    return 0;
}
