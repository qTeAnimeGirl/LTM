#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (query mask)
    uint64_t oldset = 0;
    int ret = sys_rt_sigprocmask(0 /* SIG_BLOCK */, NULL, &oldset, 8);
    TEST_ASSERT(ret == 0, "rt_sigprocmask(query) failed");

    // Case B: Boundary / Edge Cases (block SIGUSR1)
    uint64_t newset = (1ULL << (10 - 1));
    ret = sys_rt_sigprocmask(0 /* SIG_BLOCK */, &newset, &oldset, 8);
    TEST_ASSERT(ret == 0, "rt_sigprocmask(block) failed");
    // unblock
    ret = sys_rt_sigprocmask(1 /* SIG_UNBLOCK */, &newset, NULL, 8);
    TEST_ASSERT(ret == 0, "rt_sigprocmask(unblock) failed");

    // Case C: Error / Errno Handling (sigsetsize != 8)
    ret = sys_rt_sigprocmask(0, &newset, &oldset, 16);
    TEST_ASSERT(ret == -EINVAL, "rt_sigprocmask(sigsetsize!=8) must return -EINVAL");

    TEST_PASS("test_sys_135_rt_sigprocmask passed!");
    return 0;
}
