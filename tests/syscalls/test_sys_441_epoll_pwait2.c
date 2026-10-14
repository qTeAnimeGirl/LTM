#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int epfd = sys_epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1 failed");

    struct linux_epoll_event evs[4];
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000; /* 1 millisecond */

    // Case A: Happy Path (Immediate timeout wait on empty epoll set)
    int ret = sys_epoll_pwait2(epfd, evs, 4, &ts, NULL);
    TEST_ASSERT(ret == 0, "epoll_pwait2 returned unexpected events");

    // Case B: Boundary / Edge Cases (NULL timeout means block until event or immediate return on maxevents=0)
    ts.tv_nsec = 0;
    ret = sys_epoll_pwait2(epfd, evs, 4, &ts, NULL);
    TEST_ASSERT(ret == 0, "epoll_pwait2 with 0 timeout failed");

    // Case C: Error / Errno Handling (Bad epfd -> -EBADF, invalid timespec -> -EINVAL)
    ret = sys_epoll_pwait2(-1, evs, 4, &ts, NULL);
    TEST_ASSERT(ret == -EBADF, "epoll_pwait2(-1) must return -EBADF");

    struct timespec bad_ts;
    bad_ts.tv_sec = 0;
    bad_ts.tv_nsec = 2000000000L; /* Invalid nanoseconds >= 1s */
    ret = sys_epoll_pwait2(epfd, evs, 4, &bad_ts, NULL);
    TEST_ASSERT(ret == -EINVAL, "epoll_pwait2(bad nsec) must return -EINVAL");

    sys_close(epfd);
    TEST_PASS("test_sys_441_epoll_pwait2 passed!");
    return 0;
}
