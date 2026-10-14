#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    uint64_t mask = (1ULL << 14);

    // Case A: Happy Path
    int sfd = sys_signalfd4(-1, &mask, 8, 0);
    TEST_ASSERT(sfd >= 0, "signalfd4 failed");
    sys_close(sfd);

    // Case B: Boundary / Edge Cases
    sfd = sys_signalfd4(-1, &mask, 8, SFD_NONBLOCK | SFD_CLOEXEC);
    TEST_ASSERT(sfd >= 0, "signalfd4(NONBLOCK|CLOEXEC) failed");
    sys_close(sfd);

    // Case C: Error / Errno Handling
    int ret = sys_signalfd4(-1, NULL, 8, 0);
    TEST_ASSERT(ret == -EFAULT, "signalfd4(NULL) must return -EFAULT");
    ret = sys_signalfd4(-1, &mask, 4, 0);
    TEST_ASSERT(ret == -EINVAL, "signalfd4(sizemask!=8) must return -EINVAL");
    ret = sys_signalfd4(-999, &mask, 8, 0);
    TEST_ASSERT(ret == -EBADF, "signalfd4(bad_ufd) must return -EBADF");

    TEST_PASS("test_sys_074_signalfd4 passed!");
    return 0;
}
