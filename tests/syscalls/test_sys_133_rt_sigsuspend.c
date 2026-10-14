#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case C: Error / Errno Translation
    int ret = sys_rt_sigsuspend(NULL, 8);
    TEST_ASSERT(ret == -EFAULT, "rt_sigsuspend(NULL) must return -EFAULT");

    uint64_t mask = 0;
    ret = sys_rt_sigsuspend(&mask, 4);
    TEST_ASSERT(ret == -EINVAL, "rt_sigsuspend(sigsetsize=4) must return -EINVAL");

    // Case B: Boundary / Edge Cases (Various invalid sigsetsizes)
    ret = sys_rt_sigsuspend(&mask, 0);
    TEST_ASSERT(ret == -EINVAL, "rt_sigsuspend(sigsetsize=0) must return -EINVAL");

    ret = sys_rt_sigsuspend(&mask, 16);
    TEST_ASSERT(ret == -EINVAL, "rt_sigsuspend(sigsetsize=16) must return -EINVAL");

    // Case A: Happy Path (Child suspends until signal delivered)
    int pid = sys_fork();
    TEST_ASSERT(pid >= 0, "fork failed");

    if (pid == 0) {
        uint64_t child_mask = 0;
        sys_rt_sigsuspend(&child_mask, 8);
        sys_exit(0);
    }

    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = 20000000; // 20ms
    sys_nanosleep(&req, NULL);

    ret = sys_kill(pid, 15 /* SIGTERM */);
    TEST_ASSERT(ret == 0, "kill failed");

    int status = 0;
    ret = sys_wait4(pid, &status, 0, NULL);
    TEST_ASSERT(ret == pid, "wait4 failed");

    TEST_PASS("test_sys_133_rt_sigsuspend passed!");
    return 0;
}
