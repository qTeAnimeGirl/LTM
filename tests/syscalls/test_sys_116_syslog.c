#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Query buffer size: type 10 = SYSLOG_ACTION_SIZE_BUFFER)
    int ret = sys_syslog(10, NULL, 0);
    TEST_ASSERT(ret > 0, "syslog(10) failed");

    // Case B: Boundary / Edge Cases (Read klog: type 2 = SYSLOG_ACTION_READ)
    char buf[128];
    ltm_memset(buf, 0, sizeof(buf));
    ret = sys_syslog(2, buf, sizeof(buf) - 1);
    TEST_ASSERT(ret > 0, "syslog(2) read failed");

    // Case C: Error / Errno Handling (Invalid type < 1 or > 10 -> -EINVAL, NULL buffer on read -> -EFAULT)
    ret = sys_syslog(0, buf, sizeof(buf));
    TEST_ASSERT(ret == -EINVAL, "syslog(type 0) must return -EINVAL");

    ret = sys_syslog(99, buf, sizeof(buf));
    TEST_ASSERT(ret == -EINVAL, "syslog(type 99) must return -EINVAL");

    ret = sys_syslog(2, NULL, sizeof(buf));
    TEST_ASSERT(ret == -EFAULT, "syslog(2, NULL) must return -EFAULT");

    TEST_PASS("test_sys_116_syslog passed!");
    return 0;
}
