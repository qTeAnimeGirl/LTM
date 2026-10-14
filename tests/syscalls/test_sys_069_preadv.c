#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_preadv_069.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "0123456789abcdef", 16);

    // Case A: Happy Path
    char b1[4];
    char b2[4];
    ltm_memset(b1, 0, sizeof(b1));
    ltm_memset(b2, 0, sizeof(b2));
    struct linux_iovec iov[2];
    iov[0].iov_base = b1;
    iov[0].iov_len = 4;
    iov[1].iov_base = b2;
    iov[1].iov_len = 4;

    ssize_t ret = sys_preadv(fd, iov, 2, 4);
    TEST_ASSERT(ret == 8, "preadv returned incorrect byte count");
    TEST_ASSERT(ltm_memcmp(b1, "4567", 4) == 0, "b1 mismatch");
    TEST_ASSERT(ltm_memcmp(b2, "89ab", 4) == 0, "b2 mismatch");

    // Case B: Boundary / Edge Cases (iovcnt=0, iov_len=0)
    ret = sys_preadv(fd, iov, 0, 0);
    TEST_ASSERT(ret == 0, "preadv(iovcnt=0) must return 0");

    // Case C: Error / Errno Translation
    ret = sys_preadv(-1, iov, 2, 0);
    TEST_ASSERT(ret == -EBADF, "preadv(-1) must return -EBADF");
    ret = sys_preadv(fd, NULL, 2, 0);
    TEST_ASSERT(ret == -EFAULT, "preadv(NULL) must return -EFAULT");
    ret = sys_preadv(fd, iov, 2, -1);
    TEST_ASSERT(ret == -EINVAL, "preadv(offset < 0) must return -EINVAL");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_069_preadv passed!");
    return 0;
}
