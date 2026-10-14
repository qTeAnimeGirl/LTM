#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "readahead_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    char dummy[4096];
    ltm_memset(dummy, 'R', sizeof(dummy));
    sys_write(fd, dummy, sizeof(dummy));

    // Case A: Happy Path
    ssize_t ret = sys_readahead(fd, 0, 1024);
    TEST_ASSERT(ret == 0, "readahead failed");

    // Case B: Boundary / Edge Cases
    ret = sys_readahead(fd, 0, 0);
    TEST_ASSERT(ret == 0, "readahead(count=0) must return 0");

    // Case C: Error / Errno Handling
    ret = sys_readahead(-1, 0, 1024);
    TEST_ASSERT(ret == -EBADF, "readahead(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_213_readahead passed!");
    return 0;
}
