#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char attr_buf[64];
    ltm_memset(attr_buf, 0, sizeof(attr_buf));
    *(uint32_t *)attr_buf = 48; // size

    // Case A: Happy Path (Set scheduling attributes for current PID)
    int ret = sys_sched_setattr(sys_getpid(), attr_buf, 0);
    TEST_ASSERT(ret == 0, "sched_setattr failed");

    // Case B: Boundary / Edge Cases (Set for pid 0)
    ret = sys_sched_setattr(0, attr_buf, 0);
    TEST_ASSERT(ret == 0, "sched_setattr(pid=0) failed");

    // Case C: Error / Errno Handling (NULL attr buffer)
    ret = sys_sched_setattr(0, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "sched_setattr(NULL) must return -EFAULT");

    TEST_PASS("test_sys_274_sched_setattr passed!");
    return 0;
}
