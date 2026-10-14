#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int tfd = sys_timerfd_create(CLOCK_MONOTONIC, 0);
    TEST_ASSERT(tfd >= 0, "timerfd_create failed");
    sys_close(tfd);

    // Case B: Boundary / Edge Cases
    tfd = sys_timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
    TEST_ASSERT(tfd >= 0, "timerfd_create(NONBLOCK|CLOEXEC) failed");
    sys_close(tfd);

    // Case C: Error / Errno Handling
    int ret = sys_timerfd_create(9999, 0);
    TEST_ASSERT(ret == -EINVAL, "timerfd_create(invalid_clock) must return -EINVAL");

    TEST_PASS("test_sys_085_timerfd_create passed!");
    return 0;
}
