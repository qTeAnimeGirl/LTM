#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char attr_buf[64];
    ltm_memset(attr_buf, 0, sizeof(attr_buf));

    // Case A: Happy Path (Query scheduling attributes)
    int ret = sys_sched_getattr(0, attr_buf, sizeof(attr_buf), 0);
    TEST_ASSERT(ret == 0, "sched_getattr failed");
    TEST_ASSERT(*(uint32_t *)attr_buf == 48, "sched_getattr size mismatch");

    // Case B: Boundary / Edge Cases (Query for explicit PID)
    ret = sys_sched_getattr(sys_getpid(), attr_buf, sizeof(attr_buf), 0);
    TEST_ASSERT(ret == 0, "sched_getattr(getpid) failed");

    // Case C: Error / Errno Handling (NULL buffer & size too small)
    ret = sys_sched_getattr(0, NULL, sizeof(attr_buf), 0);
    TEST_ASSERT(ret == -EFAULT, "sched_getattr(NULL) must return -EFAULT");

    ret = sys_sched_getattr(0, attr_buf, 20 /* less than 48 */, 0);
    TEST_ASSERT(ret == -EINVAL, "sched_getattr(size < 48) must return -EINVAL");

    TEST_PASS("test_sys_275_sched_getattr passed!");
    return 0;
}
