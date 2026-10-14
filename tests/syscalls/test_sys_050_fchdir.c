#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int fd = sys_openat(AT_FDCWD, "/", O_RDONLY | O_DIRECTORY, 0);
    TEST_ASSERT(fd >= 0, "openat('/') failed");

    // Case A: Happy Path
    int ret = sys_fchdir(fd);
    TEST_ASSERT(ret == 0, "fchdir(fd) failed");

    // Case B: Boundary / Edge Cases (fchdir again)
    ret = sys_fchdir(fd);
    TEST_ASSERT(ret == 0, "fchdir(fd) repeat failed");

    // Case C: Error / Errno Handling
    ret = sys_fchdir(-1);
    TEST_ASSERT(ret == -EBADF, "fchdir(-1) must return -EBADF");
    ret = sys_fchdir(9999);
    TEST_ASSERT(ret == -EBADF, "fchdir(9999) must return -EBADF");

    sys_close(fd);

    TEST_PASS("test_sys_050_fchdir passed!");
    return 0;
}
