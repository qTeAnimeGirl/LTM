#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int pfd = sys_pidfd_open(sys_getpid(), 0);
    TEST_ASSERT(pfd >= 0, "pidfd_open failed");

    // Case A: Happy Path (Duplicate stdout fd via pidfd)
    int dup_fd = sys_pidfd_getfd(pfd, 1, 0);
    TEST_ASSERT(dup_fd >= 0, "pidfd_getfd(1) failed");
    sys_close(dup_fd);

    // Case B: Boundary / Edge Cases (Duplicate stderr fd 2)
    int dup_fd2 = sys_pidfd_getfd(pfd, 2, 0);
    TEST_ASSERT(dup_fd2 >= 0, "pidfd_getfd(2) failed");
    sys_close(dup_fd2);

    // Case C: Error / Errno Handling (Bad pidfd, bad targetfd, flags != 0)
    int ret = sys_pidfd_getfd(-1, 1, 0);
    TEST_ASSERT(ret == -EBADF, "pidfd_getfd(-1) must return -EBADF");

    ret = sys_pidfd_getfd(pfd, -1, 0);
    TEST_ASSERT(ret == -EBADF, "pidfd_getfd(targetfd=-1) must return -EBADF");

    ret = sys_pidfd_getfd(pfd, 1, 1 /* flags != 0 */);
    TEST_ASSERT(ret == -EINVAL, "pidfd_getfd(flags != 0) must return -EINVAL");

    sys_close(pfd);

    TEST_PASS("test_sys_438_pidfd_getfd passed!");
    return 0;
}
