#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "dup_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int dupfd = sys_dup(fd);
    TEST_ASSERT(dupfd >= 0, "dup failed");
    TEST_ASSERT(dupfd != fd, "dupfd must be different from fd");
    ssize_t w = sys_write(dupfd, "DUP_OK", 6);
    TEST_ASSERT(w == 6, "write to dupfd failed");

    // Case B: Boundary / Edge Cases (sequential dups)
    int dupfd2 = sys_dup(dupfd);
    TEST_ASSERT(dupfd2 >= 0, "second dup failed");
    sys_close(dupfd2);

    // Case C: Error / Errno Handling
    int ret = sys_dup(-1);
    TEST_ASSERT(ret == -EBADF, "dup(-1) must return -EBADF");
    ret = sys_dup(9999);
    TEST_ASSERT(ret == -EBADF, "dup(9999) must return -EBADF");

    sys_close(dupfd);
    sys_close(fd);
    sys_unlinkat(AT_FDCWD, tmp, 0);

    TEST_PASS("test_sys_023_dup passed!");
    return 0;
}
