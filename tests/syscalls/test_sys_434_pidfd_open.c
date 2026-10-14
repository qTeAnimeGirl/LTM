#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Open pidfd for current process)
    int pid = sys_getpid();
    int pfd = sys_pidfd_open(pid, 0);
    TEST_ASSERT(pfd >= 0, "pidfd_open(getpid(), 0) failed");
    sys_close(pfd);

    // Case B: Boundary / Edge Cases (Open pidfd with PIDFD_NONBLOCK)
    pfd = sys_pidfd_open(pid, PIDFD_NONBLOCK);
    TEST_ASSERT(pfd >= 0, "pidfd_open(getpid(), PIDFD_NONBLOCK) failed");
    sys_close(pfd);

    // Case C: Error / Errno Translation
    int ret = sys_pidfd_open(-1, 0);
    TEST_ASSERT(ret == -EINVAL, "pidfd_open(negative pid) must return -EINVAL");

    ret = sys_pidfd_open(0, 0);
    TEST_ASSERT(ret == -EINVAL, "pidfd_open(pid 0) must return -EINVAL");

    ret = sys_pidfd_open(9999999, 0);
    TEST_ASSERT(ret == -ESRCH, "pidfd_open(nonexistent pid) must return -ESRCH");

    ret = sys_pidfd_open(pid, 0x1234);
    TEST_ASSERT(ret == -EINVAL, "pidfd_open(invalid flags) must return -EINVAL");

    TEST_PASS("test_sys_434_pidfd_open passed!");
    return 0;
}
