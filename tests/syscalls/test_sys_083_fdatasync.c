#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fdatasync_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_fdatasync(fd);
    TEST_ASSERT(ret == 0, "fdatasync(fd) failed");

    // Case B: Boundary / Edge Cases
    sys_write(fd, "DATA", 4);
    ret = sys_fdatasync(fd);
    TEST_ASSERT(ret == 0, "fdatasync after write failed");

    // Case C: Error / Errno Handling
    ret = sys_fdatasync(-1);
    TEST_ASSERT(ret == -EBADF, "fdatasync(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_083_fdatasync passed!");
    return 0;
}
