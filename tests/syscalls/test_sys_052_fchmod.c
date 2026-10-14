#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fchmod_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_fchmod(fd, 0600);
    TEST_ASSERT(ret == 0, "fchmod(0600) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fchmod(fd, 0700);
    TEST_ASSERT(ret == 0, "fchmod(0700) failed");

    // Case C: Error / Errno Handling
    ret = sys_fchmod(-1, 0644);
    TEST_ASSERT(ret == -EBADF, "fchmod(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_052_fchmod passed!");
    return 0;
}
