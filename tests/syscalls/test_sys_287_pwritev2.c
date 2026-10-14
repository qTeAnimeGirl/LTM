#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    const char *tmp_file = "tmp_sys_287_pwritev2.tmp";
    int fd = sys_openat(AT_FDCWD, tmp_file, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path (Positional write at offset 0)
    char chunk1[] = "ABCD";
    char chunk2[] = "EFGH";
    struct iovec iov[2];
    iov[0].iov_base = chunk1;
    iov[0].iov_len = 4;
    iov[1].iov_base = chunk2;
    iov[1].iov_len = 4;

    ssize_t n = sys_pwritev2(fd, iov, 2, 0, 0);
    TEST_ASSERT(n == 8, "pwritev2 write count mismatch");

    // Case B: Boundary / Edge Cases (Offset == -1 writes at current offset)
    char chunk3[] = "IJKL";
    struct iovec iov2[1];
    iov2[0].iov_base = chunk3;
    iov2[0].iov_len = 4;
    n = sys_pwritev2(fd, iov2, 1, -1, 0);
    TEST_ASSERT(n == 4, "pwritev2(offset=-1) write count mismatch");

    // Case C: Error / Errno Translation
    n = sys_pwritev2(-1, iov, 2, 0, 0);
    TEST_ASSERT(n == -EBADF, "pwritev2(bad fd) must return -EBADF");

    n = sys_pwritev2(fd, NULL, 2, 0, 0);
    TEST_ASSERT(n == -EFAULT, "pwritev2(NULL iov) must return -EFAULT");

    n = sys_pwritev2(fd, iov, -1, 0, 0);
    TEST_ASSERT(n == -EINVAL, "pwritev2(negative count) must return -EINVAL");

    n = sys_pwritev2(fd, iov, 2, -2, 0);
    TEST_ASSERT(n == -EINVAL, "pwritev2(offset < -1) must return -EINVAL");

    n = sys_pwritev2(fd, iov, 2, 0, 0x1234);
    TEST_ASSERT(n == -EINVAL, "pwritev2(invalid flags) must return -EINVAL");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, tmp_file, 0);

    TEST_PASS("test_sys_287_pwritev2 passed!");
    return 0;
}
