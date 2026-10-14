#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Query action avail: SECCOMP_GET_ACTION_AVAIL = 2)
    unsigned int action = 0x7fff0000; /* SECCOMP_RET_ALLOW */
    int ret = sys_seccomp(2 /* SECCOMP_GET_ACTION_AVAIL */, 0, &action);
    TEST_ASSERT(ret == 0, "seccomp(GET_ACTION_AVAIL) failed");

    // Case B: Boundary / Edge Cases (Query another action)
    action = 0x00000000; /* SECCOMP_RET_KILL_PROCESS */
    ret = sys_seccomp(2, 0, &action);
    TEST_ASSERT(ret == 0, "seccomp(GET_ACTION_AVAIL kill) failed");

    // Case C: Error / Errno Handling (Invalid operation -> -EINVAL)
    ret = sys_seccomp(999, 0, NULL);
    TEST_ASSERT(ret == -EINVAL, "seccomp(invalid op) must return -EINVAL");

    TEST_PASS("test_sys_277_seccomp passed!");
    return 0;
}
