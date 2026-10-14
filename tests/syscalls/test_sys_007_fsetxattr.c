#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_007.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);

    // Case A: Happy Path
    int ret = sys_fsetxattr(fd, "user.ltm_f", "fhello", 6, 0);
    TEST_ASSERT(ret == 0, "fsetxattr happy path failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fsetxattr(fd, "user.ltm_f", "fval2", 5, XATTR_REPLACE);
    TEST_ASSERT(ret == 0, "fsetxattr replace failed");

    // Case C: Error / Errno Translation
    ret = sys_fsetxattr(-1, "user.ltm_f", "val", 3, 0);
    TEST_ASSERT(ret == -EBADF, "fsetxattr(-1) must return -EBADF");
    ret = sys_fsetxattr(fd, NULL, "val", 3, 0);
    TEST_ASSERT(ret == -EFAULT, "fsetxattr(NULL name) must return -EFAULT");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_007_fsetxattr passed!");
    return 0;
}
