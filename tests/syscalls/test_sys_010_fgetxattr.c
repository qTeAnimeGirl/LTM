#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_010.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);

    sys_fsetxattr(fd, "user.ltm_fget", "fgetme", 6, 0);

    // Case A: Happy Path
    char buf[64];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t ret = sys_fgetxattr(fd, "user.ltm_fget", buf, sizeof(buf));
    TEST_ASSERT(ret == 6, "fgetxattr length must match 6");
    TEST_ASSERT(ltm_memcmp(buf, "fgetme", 6) == 0, "fgetxattr content mismatch");

    // Case B: Boundary / Edge Cases (size=0)
    ret = sys_fgetxattr(fd, "user.ltm_fget", NULL, 0);
    TEST_ASSERT(ret == 6, "fgetxattr(size=0) must return attribute length 6");

    // Case C: Error / Errno Translation
    ret = sys_fgetxattr(-1, "user.ltm_fget", buf, sizeof(buf));
    TEST_ASSERT(ret == -EBADF, "fgetxattr(-1) must return -EBADF");
    ret = sys_fgetxattr(fd, "user.nonexistent_fattr", buf, sizeof(buf));
    TEST_ASSERT(ret == -ENODATA, "fgetxattr(nonexistent attr) must return -ENODATA");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_010_fgetxattr passed!");
    return 0;
}
