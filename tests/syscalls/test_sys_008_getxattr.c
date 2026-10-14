#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_008.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);
    sys_close(fd);

    sys_setxattr(path, "user.ltm_get", "getme", 5, 0);

    // Case A: Happy Path
    char buf[64];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t ret = sys_getxattr(path, "user.ltm_get", buf, sizeof(buf));
    TEST_ASSERT(ret == 5, "getxattr length must match 5");
    TEST_ASSERT(ltm_memcmp(buf, "getme", 5) == 0, "getxattr content mismatch");

    // Case B: Boundary / Edge Cases (size=0 queries size)
    ret = sys_getxattr(path, "user.ltm_get", NULL, 0);
    TEST_ASSERT(ret == 5, "getxattr(size=0) must return attribute length 5");

    // Case C: Error / Errno Translation
    ret = sys_getxattr(NULL, "user.ltm_get", buf, sizeof(buf));
    TEST_ASSERT(ret == -EFAULT, "getxattr(NULL path) must return -EFAULT");
    ret = sys_getxattr(path, "user.nonexistent_xattr", buf, sizeof(buf));
    TEST_ASSERT(ret == -ENODATA, "getxattr(nonexistent attr) must return -ENODATA");

    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_008_getxattr passed!");
    return 0;
}
