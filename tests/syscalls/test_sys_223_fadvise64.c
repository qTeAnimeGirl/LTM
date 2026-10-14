#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_fadv_223.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path (POSIX_FADV_NORMAL = 0)
    int ret = sys_fadvise64(fd, 0, 0, 0);
    TEST_ASSERT(ret == 0, "fadvise64(POSIX_FADV_NORMAL) failed");

    // Case B: Boundary / Edge Cases (POSIX_FADV_SEQUENTIAL = 2)
    ret = sys_fadvise64(fd, 0, 1024, 2);
    TEST_ASSERT(ret == 0, "fadvise64(POSIX_FADV_SEQUENTIAL) failed");

    // Case C: Error / Errno Translation
    ret = sys_fadvise64(fd, 0, 0, 99);
    TEST_ASSERT(ret == -EINVAL, "fadvise64(invalid advice) must return -EINVAL");
    ret = sys_fadvise64(-1, 0, 0, 0);
    TEST_ASSERT(ret == -EBADF, "fadvise64(fd=-1) must return -EBADF");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, path, 0);

    TEST_PASS("test_sys_223_fadvise64 passed!");
    return 0;
}
