#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fchown_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path (-1, -1 no-op)
    int ret = sys_fchown(fd, -1, -1);
    TEST_ASSERT(ret == 0, "fchown(-1, -1) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fchown(fd, 0, 0);
    TEST_ASSERT(ret == 0, "fchown(0, 0) failed");

    // Case C: Error / Errno Handling
    ret = sys_fchown(-1, 0, 0);
    TEST_ASSERT(ret == -EBADF, "fchown(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_054_fchown passed!");
    return 0;
}
