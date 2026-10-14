#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "close_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_close(fd);
    TEST_ASSERT(ret == 0, "close(fd) failed");

    // Case B: Boundary / Edge Cases (double close)
    ret = sys_close(fd);
    TEST_ASSERT(ret == -EBADF, "double close must return -EBADF");

    // Case C: Error / Errno Handling
    ret = sys_close(-1);
    TEST_ASSERT(ret == -EBADF, "close(-1) must return -EBADF");

    TEST_PASS("test_sys_057_close passed!");
    return 0;
}
