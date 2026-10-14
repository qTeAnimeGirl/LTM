#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "pwrite64_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    ssize_t w = sys_pwrite64(fd, "PWRITE_TEST", 11, 100);
    TEST_ASSERT(w == 11, "pwrite64 failed");
    off_t pos = sys_lseek(fd, 0, SEEK_CUR);
    TEST_ASSERT(pos == 0, "pwrite64 changed offset");

    // Case B: Boundary / Edge Cases
    w = sys_pwrite64(fd, "PWRITE_TEST", 0, 100);
    TEST_ASSERT(w == 0, "pwrite64(0) must return 0");

    // Case C: Error / Errno Handling
    w = sys_pwrite64(-1, "PWRITE_TEST", 11, 100);
    TEST_ASSERT(w == -EBADF, "pwrite64(-1) must return -EBADF");
    w = sys_pwrite64(fd, NULL, 11, 100);
    TEST_ASSERT(w == -EFAULT, "pwrite64(NULL) must return -EFAULT");
    w = sys_pwrite64(fd, "PWRITE_TEST", 11, -5);
    TEST_ASSERT(w == -EINVAL, "pwrite64(negative) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_068_pwrite64 passed!");
    return 0;
}
