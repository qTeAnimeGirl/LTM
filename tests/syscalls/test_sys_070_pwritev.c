#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_pwritev_070.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    struct linux_iovec iov[2];
    iov[0].iov_base = "WAVE";
    iov[0].iov_len = 4;
    iov[1].iov_base = "THREE";
    iov[1].iov_len = 5;

    ssize_t ret = sys_pwritev(fd, iov, 2, 0);
    TEST_ASSERT(ret == 9, "pwritev returned incorrect count");

    char buf[16];
    ltm_memset(buf, 0, sizeof(buf));
    sys_lseek(fd, 0, SEEK_SET);
    sys_read(fd, buf, 9);
    TEST_ASSERT(ltm_memcmp(buf, "WAVETHREE", 9) == 0, "written data mismatch");

    // Case B: Boundary / Edge Cases
    ret = sys_pwritev(fd, iov, 0, 0);
    TEST_ASSERT(ret == 0, "pwritev(iovcnt=0) must return 0");

    // Case C: Error / Errno Translation
    ret = sys_pwritev(-1, iov, 2, 0);
    TEST_ASSERT(ret == -EBADF, "pwritev(-1) must return -EBADF");
    ret = sys_pwritev(fd, NULL, 2, 0);
    TEST_ASSERT(ret == -EFAULT, "pwritev(NULL) must return -EFAULT");
    ret = sys_pwritev(fd, iov, 2, -1);
    TEST_ASSERT(ret == -EINVAL, "pwritev(offset < 0) must return -EINVAL");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, path, 0);
    TEST_PASS("test_sys_070_pwritev passed!");
    return 0;
}
