#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fcntl_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path (F_GETFD / F_SETFD)
    int flags = sys_fcntl(fd, 1 /* F_GETFD */, 0);
    TEST_ASSERT(flags >= 0, "fcntl(F_GETFD) failed");
    int ret = sys_fcntl(fd, 2 /* F_SETFD */, 1 /* FD_CLOEXEC */);
    TEST_ASSERT(ret == 0, "fcntl(F_SETFD) failed");
    flags = sys_fcntl(fd, 1 /* F_GETFD */, 0);
    TEST_ASSERT(flags == 1, "fcntl FD_CLOEXEC not set");

    // Case B: Boundary / Edge Cases (F_DUPFD)
    int dupfd = sys_fcntl(fd, 0 /* F_DUPFD */, 50);
    TEST_ASSERT(dupfd >= 50, "fcntl(F_DUPFD) failed");
    sys_close(dupfd);

    // Case C: Error / Errno Handling
    ret = sys_fcntl(-1, 1 /* F_GETFD */, 0);
    TEST_ASSERT(ret == -EBADF, "fcntl(-1) must return -EBADF");
    ret = sys_fcntl(fd, 99999, 0);
    TEST_ASSERT(ret == -EINVAL, "fcntl(invalid_cmd) must return -EINVAL");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, tmp, 0);

    TEST_PASS("test_sys_025_fcntl passed!");
    return 0;
}
