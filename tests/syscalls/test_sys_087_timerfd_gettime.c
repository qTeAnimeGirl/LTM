#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int tfd = sys_timerfd_create(CLOCK_MONOTONIC, 0);
    TEST_ASSERT(tfd >= 0, "timerfd_create failed");

    struct linux_itimerspec new_val;
    new_val.it_interval.tv_sec = 1;
    new_val.it_interval.tv_nsec = 0;
    new_val.it_value.tv_sec = 5;
    new_val.it_value.tv_nsec = 0;
    sys_timerfd_settime(tfd, 0, &new_val, NULL);

    // Case A: Happy Path
    struct linux_itimerspec curr;
    int ret = sys_timerfd_gettime(tfd, &curr);
    TEST_ASSERT(ret == 0, "timerfd_gettime failed");
    TEST_ASSERT(curr.it_value.tv_sec > 0 || curr.it_value.tv_nsec > 0, "timerfd_gettime value zero");

    // Case B: Boundary / Edge Cases (disarmed timer)
    new_val.it_value.tv_sec = 0;
    sys_timerfd_settime(tfd, 0, &new_val, NULL);
    ret = sys_timerfd_gettime(tfd, &curr);
    TEST_ASSERT(ret == 0, "timerfd_gettime on disarmed failed");

    // Case C: Error / Errno Handling
    ret = sys_timerfd_gettime(-1, &curr);
    TEST_ASSERT(ret == -EBADF, "timerfd_gettime(-1) must return -EBADF");
    ret = sys_timerfd_gettime(tfd, NULL);
    TEST_ASSERT(ret == -EFAULT, "timerfd_gettime(NULL) must return -EFAULT");

    sys_close(tfd);
    TEST_PASS("test_sys_087_timerfd_gettime passed!");
    return 0;
}
