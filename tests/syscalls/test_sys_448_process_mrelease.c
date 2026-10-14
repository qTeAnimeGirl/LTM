#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Calling on non-pidfd descriptor returns -EBADF)
    int ret = sys_process_mrelease(1, 0);
    TEST_ASSERT(ret == -EBADF, "process_mrelease on non-pidfd must return -EBADF");

    // Case B: Boundary / Edge Cases (fd=2)
    ret = sys_process_mrelease(2, 0);
    TEST_ASSERT(ret == -EBADF, "process_mrelease(fd=2) must return -EBADF");

    // Case C: Error / Errno Handling (Negative fd -> -EBADF, bad flags -> -EINVAL)
    ret = sys_process_mrelease(-1, 0);
    TEST_ASSERT(ret == -EBADF, "process_mrelease(-1) must return -EBADF");

    ret = sys_process_mrelease(1, 999);
    TEST_ASSERT(ret == -EINVAL, "process_mrelease(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_448_process_mrelease passed!");
    return 0;
}
