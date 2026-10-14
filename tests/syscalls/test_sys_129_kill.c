#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int my_pid = sys_getpid();

    // Case A: Happy Path (sig 0 checks process existence)
    int ret = sys_kill(my_pid, 0);
    TEST_ASSERT(ret == 0, "kill(my_pid, 0) failed");

    // Case B: Boundary / Edge Cases (kill self with 0)
    ret = sys_kill(0, 0);
    TEST_ASSERT(ret == 0, "kill(0, 0) failed");

    // Case C: Error / Errno Handling
    ret = sys_kill(my_pid, 999);
    TEST_ASSERT(ret == -EINVAL, "kill(invalid_sig) must return -EINVAL");
    ret = sys_kill(-99999, 0);
    TEST_ASSERT(ret == -ESRCH, "kill(nonexistent_pid) must return -ESRCH");

    TEST_PASS("test_sys_129_kill passed!");
    return 0;
}
