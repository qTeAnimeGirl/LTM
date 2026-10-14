#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Calling register on non-ring descriptor returns -EBADF)
    int ret = sys_io_uring_register(1, 0, NULL, 0);
    TEST_ASSERT(ret == -EBADF, "io_uring_register on non-ring fd must return -EBADF");

    // Case B: Boundary / Edge Cases (Valid opcode 1)
    ret = sys_io_uring_register(2, 1, NULL, 0);
    TEST_ASSERT(ret == -EBADF, "io_uring_register(opcode=1) must return -EBADF");

    // Case C: Error / Errno Handling (Bad negative fd -> -EBADF, invalid opcode -> -EINVAL)
    ret = sys_io_uring_register(-1, 0, NULL, 0);
    TEST_ASSERT(ret == -EBADF, "io_uring_register(-1) must return -EBADF");

    ret = sys_io_uring_register(1, 9999, NULL, 0);
    TEST_ASSERT(ret == -EINVAL, "io_uring_register(bad opcode) must return -EINVAL");

    TEST_PASS("test_sys_427_io_uring_register passed!");
    return 0;
}
