#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (epoll_create1 with 0)
    int epfd = sys_epoll_create1(0);
    TEST_ASSERT(epfd >= 0, "epoll_create1(0) failed");
    sys_close(epfd);

    // Case B: Boundary / Edge Cases (EPOLL_CLOEXEC)
    epfd = sys_epoll_create1(02000000 /* EPOLL_CLOEXEC */);
    TEST_ASSERT(epfd >= 0, "epoll_create1(EPOLL_CLOEXEC) failed");
    sys_close(epfd);

    // Case C: Error / Errno Handling (invalid flags)
    int ret = sys_epoll_create1(999999);
    TEST_ASSERT(ret == -EINVAL, "epoll_create1(invalid_flags) must return -EINVAL");

    TEST_PASS("test_sys_020_epoll_create1 passed!");
    return 0;
}
