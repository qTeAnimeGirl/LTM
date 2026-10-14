#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "write_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    const char *msg = "HELLO_WRITE";
    ssize_t w = sys_write(fd, msg, 11);
    TEST_ASSERT(w == 11, "write failed");

    // Case B: Boundary / Edge Cases
    w = sys_write(fd, msg, 0);
    TEST_ASSERT(w == 0, "write(count=0) must return 0");

    // Case C: Error / Errno Handling
    w = sys_write(-1, msg, 11);
    TEST_ASSERT(w == -EBADF, "write(-1) must return -EBADF");
    w = sys_write(fd, NULL, 11);
    TEST_ASSERT(w == -EFAULT, "write(NULL) must return -EFAULT");

    sys_close(fd);
    TEST_PASS("test_sys_064_write passed!");
    return 0;
}
