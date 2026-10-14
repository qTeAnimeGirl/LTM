#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_006.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_lsetxattr(path, "user.ltm_ltest", "lhello", 6, 0);
    TEST_ASSERT(ret == 0, "lsetxattr happy path failed");

    // Case B: Boundary / Edge Cases
    ret = sys_lsetxattr(path, "user.ltm_ltest", "lval2", 5, XATTR_REPLACE);
    TEST_ASSERT(ret == 0, "lsetxattr replace failed");

    // Case C: Error / Errno Translation
    ret = sys_lsetxattr(NULL, "user.ltm_ltest", "val", 3, 0);
    TEST_ASSERT(ret == -EFAULT, "lsetxattr(NULL path) must return -EFAULT");
    ret = sys_lsetxattr("nonexistent_file_xattr_006", "user.ltm", "val", 3, 0);
    TEST_ASSERT(ret == -ENOENT, "lsetxattr(nonexistent) must return -ENOENT");

    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_006_lsetxattr passed!");
    return 0;
}
