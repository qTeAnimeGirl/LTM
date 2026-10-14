#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "utimensat_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    // Case A: Happy Path (times == NULL sets to current time)
    int ret = sys_utimensat(AT_FDCWD, tmp, NULL, 0);
    TEST_ASSERT(ret == 0, "utimensat(NULL) failed");

    // Case B: Boundary / Edge Cases (explicit times)
    struct linux_timespec ts[2];
    ts[0].tv_sec = 1000000; ts[0].tv_nsec = 0;
    ts[1].tv_sec = 1000000; ts[1].tv_nsec = 0;
    ret = sys_utimensat(AT_FDCWD, tmp, ts, 0);
    TEST_ASSERT(ret == 0, "utimensat(explicit) failed");

    // Case C: Error / Errno Handling
    ret = sys_utimensat(-999, tmp, NULL, 0);
    TEST_ASSERT(ret == -EBADF, "utimensat(bad_dirfd) must return -EBADF");
    ret = sys_utimensat(AT_FDCWD, "/nonexistent_utimensat_xyz", NULL, 0);
    TEST_ASSERT(ret == -ENOENT, "utimensat(nonexistent) must return -ENOENT");

    sys_unlinkat(AT_FDCWD, tmp, 0);

    TEST_PASS("test_sys_088_utimensat passed!");
    return 0;
}
