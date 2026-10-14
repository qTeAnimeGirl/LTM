#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (clone3 process with SIGCHLD)
    struct clone_args ca;
    ltm_memset(&ca, 0, sizeof(ca));
    ca.exit_signal = 17; /* SIGCHLD */

    long pid = sys_clone3(&ca, sizeof(ca));
    TEST_ASSERT(pid >= 0, "clone3 failed");
    if (pid == 0) {
        sys_exit(88);
    }

    int status = 0;
    int ret = sys_wait4((int)pid, &status, 0, NULL);
    TEST_ASSERT(ret == (int)pid, "wait4 failed");
    int exit_code = (status >> 8) & 0xff;
    TEST_ASSERT(exit_code == 88, "child exit code mismatch");

    // Case B: Boundary / Edge Cases (clone3 with size exactly 64)
    struct clone_args ca2;
    ltm_memset(&ca2, 0, sizeof(ca2));
    ca2.exit_signal = 17; /* SIGCHLD */

    long pid2 = sys_clone3(&ca2, 64);
    TEST_ASSERT(pid2 >= 0, "clone3 with size 64 failed");
    if (pid2 == 0) {
        sys_exit(99);
    }

    ret = sys_wait4((int)pid2, &status, 0, NULL);
    TEST_ASSERT(ret == (int)pid2, "wait4 failed");
    exit_code = (status >> 8) & 0xff;
    TEST_ASSERT(exit_code == 99, "child exit code mismatch");

    // Case C: Error / Errno Handling (NULL cl_args -> -EFAULT, size < 64 -> -EINVAL)
    ret = sys_clone3(NULL, sizeof(ca));
    TEST_ASSERT(ret == -EFAULT, "clone3 with NULL cl_args must return -EFAULT");

    ret = sys_clone3(&ca, 32);
    TEST_ASSERT(ret == -EINVAL, "clone3 with size < 64 must return -EINVAL");

    TEST_PASS("test_sys_435_clone3 passed!");
    return 0;
}
