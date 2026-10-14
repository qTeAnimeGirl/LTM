#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int epfd = sys_epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1 failed");

    int p[2];
    int ret = sys_pipe2(p, 0);
    TEST_ASSERT(ret == 0, "pipe2 failed");

    struct linux_epoll_event ev;
    ev.events = 1; // EPOLLIN
    ev.data = 999;
    ret = sys_epoll_ctl(epfd, 1 /* ADD */, p[0], &ev);
    TEST_ASSERT(ret == 0, "epoll_ctl(ADD) failed");

    // Write to pipe to trigger event
    sys_write(p[1], "X", 1);

    // Case A: Happy Path (wait for event)
    struct linux_epoll_event events[2];
    ret = sys_epoll_pwait(epfd, events, 2, 100, NULL);
    TEST_ASSERT(ret == 1, "epoll_pwait must return 1 event");
    TEST_ASSERT(events[0].data == 999, "event data mismatch");

    char buf[4];
    sys_read(p[0], buf, 1);

    // Case B: Boundary / Edge Cases (timeout 0 when empty)
    ret = sys_epoll_pwait(epfd, events, 2, 0, NULL);
    TEST_ASSERT(ret == 0, "epoll_pwait(empty, timeout=0) must return 0");

    // Case C: Error / Errno Handling
    ret = sys_epoll_pwait(-1, events, 2, 0, NULL);
    TEST_ASSERT(ret == -EBADF, "epoll_pwait(-1) must return -EBADF");
    ret = sys_epoll_pwait(epfd, events, 0, 0, NULL);
    TEST_ASSERT(ret == -EINVAL, "epoll_pwait(maxevents=0) must return -EINVAL");

    sys_close(p[0]);
    sys_close(p[1]);
    sys_close(epfd);

    TEST_PASS("test_sys_022_epoll_pwait passed!");
    return 0;
}
