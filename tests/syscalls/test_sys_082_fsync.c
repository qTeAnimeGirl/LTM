#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fsync_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_fsync(fd);
    TEST_ASSERT(ret == 0, "fsync(fd) failed");

    // Case B: Boundary / Edge Cases (sync after write)
    sys_write(fd, "FLUSH", 5);
    ret = sys_fsync(fd);
    TEST_ASSERT(ret == 0, "fsync after write failed");

    // Case C: Error / Errno Handling
    ret = sys_fsync(-1);
    TEST_ASSERT(ret == -EBADF, "fsync(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_082_fsync passed!");
    return 0;
}
