#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Valid fd call returns -ENOSYS as quotas are virtualized/unsupported)
    int ret = sys_quotactl_fd(1, 0, 0, NULL);
    TEST_ASSERT(ret == -ENOSYS, "quotactl_fd must return -ENOSYS");

    // Case B: Boundary / Edge Cases (Command 1)
    ret = sys_quotactl_fd(2, 1, 1000, NULL);
    TEST_ASSERT(ret == -ENOSYS, "quotactl_fd(cmd=1) must return -ENOSYS");

    // Case C: Error / Errno Handling (Negative fd -> -EBADF)
    ret = sys_quotactl_fd(-1, 0, 0, NULL);
    TEST_ASSERT(ret == -EBADF, "quotactl_fd(-1) must return -EBADF");

    TEST_PASS("test_sys_443_quotactl_fd passed!");
    return 0;
}
