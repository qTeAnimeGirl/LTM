#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int tfd = sys_timerfd_create(CLOCK_MONOTONIC, 0);
    TEST_ASSERT(tfd >= 0, "timerfd_create failed");

    struct linux_itimerspec new_val;
    new_val.it_interval.tv_sec = 0;
    new_val.it_interval.tv_nsec = 0;
    new_val.it_value.tv_sec = 10;
    new_val.it_value.tv_nsec = 0;

    struct linux_itimerspec old_val;

    // Case A: Happy Path
    int ret = sys_timerfd_settime(tfd, 0, &new_val, &old_val);
    TEST_ASSERT(ret == 0, "timerfd_settime failed");

    // Case B: Boundary / Edge Cases (disarm, old_val NULL)
    new_val.it_value.tv_sec = 0;
    ret = sys_timerfd_settime(tfd, 0, &new_val, NULL);
    TEST_ASSERT(ret == 0, "timerfd_settime disarm failed");

    // Case C: Error / Errno Handling
    ret = sys_timerfd_settime(-1, 0, &new_val, NULL);
    TEST_ASSERT(ret == -EBADF, "timerfd_settime(-1) must return -EBADF");
    ret = sys_timerfd_settime(tfd, 0, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "timerfd_settime(NULL) must return -EFAULT");

    sys_close(tfd);
    TEST_PASS("test_sys_086_timerfd_settime passed!");
    return 0;
}
