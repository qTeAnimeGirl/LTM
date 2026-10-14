#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int pfd = sys_pidfd_open(sys_getpid(), 0);
    TEST_ASSERT(pfd >= 0, "pidfd_open(getpid) failed");

    // Case A: Happy Path (Send signal 0 probe via pidfd)
    int ret = sys_pidfd_send_signal(pfd, 0, NULL, 0);
    TEST_ASSERT(ret == 0, "pidfd_send_signal(0) failed");

    // Case B: Boundary / Edge Cases (Send signal 0 to PID 1 pidfd)
    int pfd1 = sys_pidfd_open(1, 0);
    TEST_ASSERT(pfd1 >= 0, "pidfd_open(1) failed");
    ret = sys_pidfd_send_signal(pfd1, 0, NULL, 0);
    TEST_ASSERT(ret == 0, "pidfd_send_signal(pfd1, 0) failed");
    sys_close(pfd1);

    // Case C: Error / Errno Handling (Bad fd, invalid flags, invalid sig)
    ret = sys_pidfd_send_signal(-1, 0, NULL, 0);
    TEST_ASSERT(ret == -EBADF, "pidfd_send_signal(-1) must return -EBADF");

    ret = sys_pidfd_send_signal(pfd, 0, NULL, 1 /* flags != 0 */);
    TEST_ASSERT(ret == -EINVAL, "pidfd_send_signal(flags != 0) must return -EINVAL");

    ret = sys_pidfd_send_signal(pfd, 999, NULL, 0);
    TEST_ASSERT(ret == -EINVAL, "pidfd_send_signal(invalid sig) must return -EINVAL");

    sys_close(pfd);

    TEST_PASS("test_sys_424_pidfd_send_signal passed!");
    return 0;
}
