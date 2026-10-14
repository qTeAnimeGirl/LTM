#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_xattr_011.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test", 4);
    sys_close(fd);

    sys_setxattr(path, "user.list1", "v1", 2, 0);

    // Case A: Happy Path
    char list[128];
    ltm_memset(list, 0, sizeof(list));
    ssize_t ret = sys_listxattr(path, list, sizeof(list));
    TEST_ASSERT(ret > 0, "listxattr failed");

    // Case B: Boundary / Edge Cases (size=0 queries required size)
    ret = sys_listxattr(path, NULL, 0);
    TEST_ASSERT(ret > 0, "listxattr(size=0) must return non-zero size");

    // Case C: Error / Errno Translation
    ret = sys_listxattr(NULL, list, sizeof(list));
    TEST_ASSERT(ret == -EFAULT, "listxattr(NULL path) must return -EFAULT");
    ret = sys_listxattr("nonexistent_file_listxattr_xyz", list, sizeof(list));
    TEST_ASSERT(ret == -ENOENT, "listxattr(nonexistent) must return -ENOENT");

    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_011_listxattr passed!");
    return 0;
}
