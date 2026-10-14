#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int my_tid = sys_gettid();

    // Case A: Happy Path (sig 0 on current tid)
    int ret = sys_tkill(my_tid, 0);
    TEST_ASSERT(ret == 0, "tkill(my_tid, 0) failed");

    // Case B: Boundary / Edge Cases (repeated tkill)
    ret = sys_tkill(my_tid, 0);
    TEST_ASSERT(ret == 0, "tkill(my_tid, 0) repeat failed");

    // Case C: Error / Errno Handling
    ret = sys_tkill(my_tid, 999);
    TEST_ASSERT(ret == -EINVAL, "tkill(invalid_sig) must return -EINVAL");
    ret = sys_tkill(-99999, 0);
    TEST_ASSERT(ret == -ESRCH, "tkill(nonexistent_tid) must return -ESRCH");

    TEST_PASS("test_sys_130_tkill passed!");
    return 0;
}
