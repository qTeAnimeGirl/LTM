#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "flock_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_flock(fd, LOCK_EX);
    TEST_ASSERT(ret == 0, "flock(LOCK_EX) failed");
    ret = sys_flock(fd, LOCK_UN);
    TEST_ASSERT(ret == 0, "flock(LOCK_UN) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_flock(fd, LOCK_SH | LOCK_NB);
    TEST_ASSERT(ret == 0, "flock(LOCK_SH | LOCK_NB) failed");
    sys_flock(fd, LOCK_UN);

    // Case C: Error / Errno Handling
    ret = sys_flock(-1, LOCK_EX);
    TEST_ASSERT(ret == -EBADF, "flock(-1) must return -EBADF");
    ret = sys_flock(fd, 9999);
    TEST_ASSERT(ret == -EINVAL, "flock(invalid) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_032_flock passed!");
    return 0;
}
