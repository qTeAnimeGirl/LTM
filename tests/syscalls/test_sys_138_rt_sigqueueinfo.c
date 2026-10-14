#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char siginfo_buf[128];
    ltm_memset(siginfo_buf, 0, sizeof(siginfo_buf));

    // Case A: Happy Path (sig == 0 to probe process existence)
    int pid = sys_getpid();
    int ret = sys_rt_sigqueueinfo(pid, 0, siginfo_buf);
    TEST_ASSERT(ret == 0, "rt_sigqueueinfo(pid, 0) failed");

    // Case B: Boundary / Edge Cases (Send to self with valid siginfo)
    ret = sys_rt_sigqueueinfo(pid, 0, siginfo_buf);
    TEST_ASSERT(ret == 0, "rt_sigqueueinfo(self, 0) probe should succeed");

    // Case C: Error / Errno Translation
    ret = sys_rt_sigqueueinfo(pid, 0, NULL);
    TEST_ASSERT(ret == -EFAULT, "rt_sigqueueinfo(NULL uinfo) must return -EFAULT");

    ret = sys_rt_sigqueueinfo(pid, -1, siginfo_buf);
    TEST_ASSERT(ret == -EINVAL, "rt_sigqueueinfo(negative sig) must return -EINVAL");

    ret = sys_rt_sigqueueinfo(pid, 999, siginfo_buf);
    TEST_ASSERT(ret == -EINVAL, "rt_sigqueueinfo(sig >= NSIG) must return -EINVAL");

    ret = sys_rt_sigqueueinfo(9999999, 0, siginfo_buf);
    TEST_ASSERT(ret == -ESRCH, "rt_sigqueueinfo(nonexistent pid) must return -ESRCH");

    TEST_PASS("test_sys_138_rt_sigqueueinfo passed!");
    return 0;
}
