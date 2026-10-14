#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

static void dummy_handler(int sig) {
    (void)sig;
}

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (register dummy handler for SIGUSR1 = 10)
    struct linux_sigaction act, oldact;
    act.sa_handler = dummy_handler;
    act.sa_flags = 0;
    act.sa_restorer = NULL;
    act.sa_mask = 0;

    int ret = sys_rt_sigaction(10 /* SIGUSR1 */, &act, &oldact, 8);
    TEST_ASSERT(ret == 0, "rt_sigaction(SIGUSR1) failed");

    // Case B: Boundary / Edge Cases (restore old action)
    ret = sys_rt_sigaction(10, &oldact, NULL, 8);
    TEST_ASSERT(ret == 0, "rt_sigaction(restore) failed");

    // Case C: Error / Errno Handling (sigsetsize != 8)
    ret = sys_rt_sigaction(10, &act, &oldact, 4);
    TEST_ASSERT(ret == -EINVAL, "rt_sigaction(sigsetsize!=8) must return -EINVAL");
    ret = sys_rt_sigaction(9 /* SIGKILL cannot be caught */, &act, &oldact, 8);
    TEST_ASSERT(ret == -EINVAL, "rt_sigaction(SIGKILL) must return -EINVAL");

    TEST_PASS("test_sys_134_rt_sigaction passed!");
    return 0;
}
