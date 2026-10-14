#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    uint64_t mask = (1ULL << (14 /* SIGALRM */ - 1));

    // Case A: Happy Path (Wait with 0 timeout returns -EAGAIN when no signal pending)
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    int ret = sys_rt_sigtimedwait(&mask, NULL, &ts, 8);
    TEST_ASSERT(ret == -EAGAIN, "rt_sigtimedwait with zero timeout should return -EAGAIN");

    // Case B: Boundary / Edge Cases (Wait with NULL timeout pointer when no pending signals)
    // To prevent hanging, we test with small 100ns timeout
    ts.tv_sec = 0;
    ts.tv_nsec = 100;
    ret = sys_rt_sigtimedwait(&mask, NULL, &ts, 8);
    TEST_ASSERT(ret == -EAGAIN, "rt_sigtimedwait with 100ns timeout should return -EAGAIN");

    // Case C: Error / Errno Translation
    ret = sys_rt_sigtimedwait(NULL, NULL, &ts, 8);
    TEST_ASSERT(ret == -EFAULT, "rt_sigtimedwait(NULL mask) must return -EFAULT");

    ret = sys_rt_sigtimedwait(&mask, NULL, &ts, 4);
    TEST_ASSERT(ret == -EINVAL, "rt_sigtimedwait(size!=8) must return -EINVAL");

    struct timespec bad_ts;
    bad_ts.tv_sec = 0;
    bad_ts.tv_nsec = 1000000000; // >= 1e9
    ret = sys_rt_sigtimedwait(&mask, NULL, &bad_ts, 8);
    TEST_ASSERT(ret == -EINVAL, "rt_sigtimedwait(invalid nsec) must return -EINVAL");

    TEST_PASS("test_sys_137_rt_sigtimedwait passed!");
    return 0;
}
