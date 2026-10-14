#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "pread64_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_pwrite64(fd, "PREAD_TARGET", 12, 50);

    // Case A: Happy Path
    char buf[32];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t r = sys_pread64(fd, buf, 12, 50);
    TEST_ASSERT(r == 12, "pread64 failed");
    TEST_ASSERT(ltm_memcmp(buf, "PREAD_TARGET", 12) == 0, "pread64 data corrupted");
    off_t pos = sys_lseek(fd, 0, SEEK_CUR);
    TEST_ASSERT(pos == 0, "pread64 changed offset");

    // Case B: Boundary / Edge Cases
    r = sys_pread64(fd, buf, 0, 50);
    TEST_ASSERT(r == 0, "pread64(0) must return 0");

    // Case C: Error / Errno Handling
    r = sys_pread64(-1, buf, 12, 50);
    TEST_ASSERT(r == -EBADF, "pread64(-1) must return -EBADF");
    r = sys_pread64(fd, NULL, 12, 50);
    TEST_ASSERT(r == -EFAULT, "pread64(NULL) must return -EFAULT");
    r = sys_pread64(fd, buf, 12, -1);
    TEST_ASSERT(r == -EINVAL, "pread64(negative) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_067_pread64 passed!");
    return 0;
}
