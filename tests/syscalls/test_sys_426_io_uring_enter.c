#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Calling enter on non-uring descriptor returns -EBADF)
    int ret = sys_io_uring_enter(1, 0, 0, 0, NULL);
    TEST_ASSERT(ret == -EBADF, "io_uring_enter on non-ring fd must return -EBADF");

    // Case B: Boundary / Edge Cases (Zero submit and complete)
    ret = sys_io_uring_enter(2, 0, 0, 0, NULL);
    TEST_ASSERT(ret == -EBADF, "io_uring_enter(fd=2) must return -EBADF");

    // Case C: Error / Errno Handling (Bad negative fd -> -EBADF, invalid flags -> -EINVAL)
    ret = sys_io_uring_enter(-1, 0, 0, 0, NULL);
    TEST_ASSERT(ret == -EBADF, "io_uring_enter(-1) must return -EBADF");

    ret = sys_io_uring_enter(1, 0, 0, 0xff000000, NULL);
    TEST_ASSERT(ret == -EINVAL, "io_uring_enter(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_426_io_uring_enter passed!");
    return 0;
}
