#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    pid_t pid = sys_getpid();
    pid_t tid = sys_gettid();

    char dummy_info[128];
    ltm_memset(dummy_info, 0, sizeof(dummy_info));

    // Case A: Happy Path (Probe self thread group with signal 0)
    int ret = sys_rt_tgsigqueueinfo(pid, tid, 0, dummy_info);
    TEST_ASSERT(ret == 0, "rt_tgsigqueueinfo(self, 0) failed");

    // Case B: Boundary / Edge Cases (Probe virtual PID 1)
    ret = sys_rt_tgsigqueueinfo(1, 1, 0, dummy_info);
    TEST_ASSERT(ret == 0, "rt_tgsigqueueinfo(1, 1, 0) failed");

    // Case C: Error / Errno Handling (NULL uinfo, invalid sig, invalid tgid)
    ret = sys_rt_tgsigqueueinfo(pid, tid, 0, NULL);
    TEST_ASSERT(ret == -EFAULT, "rt_tgsigqueueinfo(NULL) must return -EFAULT");

    ret = sys_rt_tgsigqueueinfo(pid, tid, 999, dummy_info);
    TEST_ASSERT(ret == -EINVAL, "rt_tgsigqueueinfo(invalid sig) must return -EINVAL");

    ret = sys_rt_tgsigqueueinfo(-1, tid, 0, dummy_info);
    TEST_ASSERT(ret == -EINVAL, "rt_tgsigqueueinfo(tgid <= 0) must return -EINVAL");

    TEST_PASS("test_sys_240_rt_tgsigqueueinfo passed!");
    return 0;
}
