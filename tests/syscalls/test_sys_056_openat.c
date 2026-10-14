#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "openat_test.tmp";

    // Case A: Happy Path
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat(CREAT|RDWR) failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases
    fd = sys_openat(AT_FDCWD, tmp, O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "openat(RDONLY) failed");
    sys_close(fd);

    // Case C: Error / Errno Handling
    int ret = sys_openat(AT_FDCWD, NULL, O_RDONLY, 0);
    TEST_ASSERT(ret == -EFAULT, "openat(NULL) must return -EFAULT");
    ret = sys_openat(AT_FDCWD, "/nonexistent_openat_path_xyz", O_RDONLY, 0);
    TEST_ASSERT(ret == -ENOENT, "openat(nonexistent) must return -ENOENT");
    ret = sys_openat(-999, tmp, O_RDONLY, 0);
    TEST_ASSERT(ret == -EBADF, "openat(invalid_dirfd) must return -EBADF");

    TEST_PASS("test_sys_056_openat passed!");
    return 0;
}
