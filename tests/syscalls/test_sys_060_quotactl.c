#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Standard quotactl query returns -ENOSYS or -EPERM)
    char buf[128];
    int ret = sys_quotactl(0x800002 /* Q_GETQUOTA */, "/dev/null", 0, buf);
    TEST_ASSERT(ret == -ENOSYS || ret == -EPERM, "quotactl returned unexpected code");

    // Case B: Boundary / Edge Cases (Q_SYNC with NULL special)
    ret = sys_quotactl(0x800003 /* Q_SYNC */, NULL, 0, NULL);
    TEST_ASSERT(ret == -ENOSYS || ret == -EPERM || ret == 0, "quotactl(Q_SYNC) failed");

    // Case C: Error / Errno Handling (NULL special on non-sync command -> EFAULT)
    ret = sys_quotactl(0x800002 /* Q_GETQUOTA */, NULL, 0, buf);
    TEST_ASSERT(ret == -EFAULT, "quotactl(NULL special) must return -EFAULT");

    TEST_PASS("test_sys_060_quotactl passed!");
    return 0;
}
