#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Standard restart_syscall returns -EINTR)
    int ret = sys_restart_syscall();
    TEST_ASSERT(ret == -EINTR, "restart_syscall must return -EINTR");

    // Case B: Boundary / Edge Cases (Repeated invocation)
    ret = sys_restart_syscall();
    TEST_ASSERT(ret == -EINTR, "Repeated restart_syscall failed");

    // Case C: Error / Errno Handling (Confirm return code is strictly negative)
    TEST_ASSERT(ret < 0, "restart_syscall must return a negative error code");

    TEST_PASS("test_sys_128_restart_syscall passed!");
    return 0;
}
