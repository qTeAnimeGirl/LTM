#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "readv_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "ABCDEF", 6);
    sys_lseek(fd, 0, 0);

    // Case A: Happy Path
    char b1[3], b2[3];
    struct linux_iovec iov[2];
    iov[0].iov_base = b1; iov[0].iov_len = 3;
    iov[1].iov_base = b2; iov[1].iov_len = 3;

    ssize_t ret = sys_readv(fd, iov, 2);
    TEST_ASSERT(ret == 6, "readv failed");
    TEST_ASSERT(b1[0] == 'A' && b2[0] == 'D', "readv data incorrect");

    // Case B: Boundary / Edge Cases (iovcnt = 0)
    ret = sys_readv(fd, iov, 0);
    TEST_ASSERT(ret == 0, "readv(iovcnt=0) must return 0");

    // Case C: Error / Errno Handling
    ret = sys_readv(-1, iov, 2);
    TEST_ASSERT(ret == -EBADF, "readv(-1) must return -EBADF");
    ret = sys_readv(fd, NULL, 2);
    TEST_ASSERT(ret == -EFAULT, "readv(NULL) must return -EFAULT");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, tmp, 0);

    TEST_PASS("test_sys_065_readv passed!");
    return 0;
}
