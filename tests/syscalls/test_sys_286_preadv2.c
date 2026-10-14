#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    const char *tmp_file = "tmp_sys_286_preadv2.tmp";
    int fd = sys_openat(AT_FDCWD, tmp_file, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    char write_data[] = "0123456789abcdef";
    sys_write(fd, write_data, 16);

    // Case A: Happy Path (Positional read at offset 4)
    char buf1[4], buf2[4];
    struct iovec iov[2];
    iov[0].iov_base = buf1;
    iov[0].iov_len = 4;
    iov[1].iov_base = buf2;
    iov[1].iov_len = 4;

    ssize_t n = sys_preadv2(fd, iov, 2, 4, 0);
    TEST_ASSERT(n == 8, "preadv2 read count mismatch");
    TEST_ASSERT(ltm_memcmp(buf1, "4567", 4) == 0, "buf1 content mismatch");
    TEST_ASSERT(ltm_memcmp(buf2, "89ab", 4) == 0, "buf2 content mismatch");

    // Case B: Boundary / Edge Cases (Offset == -1 uses current file offset)
    sys_lseek(fd, 2, 0 /* SEEK_SET */);
    n = sys_preadv2(fd, iov, 1, -1, 0);
    TEST_ASSERT(n == 4, "preadv2(offset=-1) read count mismatch");
    TEST_ASSERT(ltm_memcmp(buf1, "2345", 4) == 0, "preadv2(offset=-1) content mismatch");

    // Case C: Error / Errno Translation
    n = sys_preadv2(-1, iov, 2, 0, 0);
    TEST_ASSERT(n == -EBADF, "preadv2(bad fd) must return -EBADF");

    n = sys_preadv2(fd, NULL, 2, 0, 0);
    TEST_ASSERT(n == -EFAULT, "preadv2(NULL iov) must return -EFAULT");

    n = sys_preadv2(fd, iov, -1, 0, 0);
    TEST_ASSERT(n == -EINVAL, "preadv2(negative count) must return -EINVAL");

    n = sys_preadv2(fd, iov, 2, -2, 0); // Offset < -1
    TEST_ASSERT(n == -EINVAL, "preadv2(offset < -1) must return -EINVAL");

    n = sys_preadv2(fd, iov, 2, 0, 0x1234); // Invalid flags
    TEST_ASSERT(n == -EINVAL, "preadv2(invalid flags) must return -EINVAL");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, tmp_file, 0);

    TEST_PASS("test_sys_286_preadv2 passed!");
    return 0;
}
