#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "writev_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    struct linux_iovec iov[2];
    iov[0].iov_base = "HELLO_"; iov[0].iov_len = 6;
    iov[1].iov_base = "WRITEV"; iov[1].iov_len = 6;

    ssize_t ret = sys_writev(fd, iov, 2);
    TEST_ASSERT(ret == 12, "writev failed");

    // Case B: Boundary / Edge Cases (iovcnt = 0)
    ret = sys_writev(fd, iov, 0);
    TEST_ASSERT(ret == 0, "writev(iovcnt=0) must return 0");

    // Case C: Error / Errno Handling
    ret = sys_writev(-1, iov, 2);
    TEST_ASSERT(ret == -EBADF, "writev(-1) must return -EBADF");
    ret = sys_writev(fd, NULL, 2);
    TEST_ASSERT(ret == -EFAULT, "writev(NULL) must return -EFAULT");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, tmp, 0);

    TEST_PASS("test_sys_066_writev passed!");
    return 0;
}
