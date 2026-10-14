#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_009.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);
    sys_close(fd);

    sys_lsetxattr(path, "user.ltm_lget", "lgetme", 6, 0);

    // Case A: Happy Path
    char buf[64];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t ret = sys_lgetxattr(path, "user.ltm_lget", buf, sizeof(buf));
    TEST_ASSERT(ret == 6, "lgetxattr length must match 6");
    TEST_ASSERT(ltm_memcmp(buf, "lgetme", 6) == 0, "lgetxattr content mismatch");

    // Case B: Boundary / Edge Cases (size=0)
    ret = sys_lgetxattr(path, "user.ltm_lget", NULL, 0);
    TEST_ASSERT(ret == 6, "lgetxattr(size=0) must return attribute length 6");

    // Case C: Error / Errno Translation
    ret = sys_lgetxattr(NULL, "user.ltm_lget", buf, sizeof(buf));
    TEST_ASSERT(ret == -EFAULT, "lgetxattr(NULL path) must return -EFAULT");
    ret = sys_lgetxattr(path, "user.nonexistent_lattr", buf, sizeof(buf));
    TEST_ASSERT(ret == -ENODATA, "lgetxattr(nonexistent attr) must return -ENODATA");

    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_009_lgetxattr passed!");
    return 0;
}
