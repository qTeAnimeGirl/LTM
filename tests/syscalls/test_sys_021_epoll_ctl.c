#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int epfd = sys_epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1 failed");

    int p[2];
    int ret = sys_pipe2(p, 0);
    TEST_ASSERT(ret == 0, "pipe2 failed");

    // Case A: Happy Path (EPOLL_CTL_ADD)
    struct linux_epoll_event ev;
    ev.events = 1; // EPOLLIN
    ev.data = 12345;
    ret = sys_epoll_ctl(epfd, 1 /* ADD */, p[0], &ev);
    TEST_ASSERT(ret == 0, "epoll_ctl(ADD) failed");

    // Case B: Boundary / Edge Cases (EPOLL_CTL_MOD then EPOLL_CTL_DEL)
    ev.events = 4; // EPOLLOUT
    ret = sys_epoll_ctl(epfd, 3 /* MOD */, p[0], &ev);
    TEST_ASSERT(ret == 0, "epoll_ctl(MOD) failed");

    ret = sys_epoll_ctl(epfd, 2 /* DEL */, p[0], &ev);
    TEST_ASSERT(ret == 0, "epoll_ctl(DEL) failed");

    // Case C: Error / Errno Handling
    ret = sys_epoll_ctl(-1, 1, p[0], &ev);
    TEST_ASSERT(ret == -EBADF, "epoll_ctl(-1) must return -EBADF");
    ret = sys_epoll_ctl(epfd, 2 /* DEL non-existent */, p[0], &ev);
    TEST_ASSERT(ret == -ENOENT, "epoll_ctl(DEL nonexistent) must return -ENOENT");

    sys_close(p[0]);
    sys_close(p[1]);
    sys_close(epfd);

    TEST_PASS("test_sys_021_epoll_ctl passed!");
    return 0;
}
