#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Query via clock_adjtime)
    struct timex tx;
    ltm_memset(&tx, 0, sizeof(tx));
    int ret = sys_clock_adjtime(0 /* CLOCK_REALTIME */, &tx);
    TEST_ASSERT(ret == 0, "clock_adjtime(CLOCK_REALTIME) failed");

    // Case B: Boundary / Edge Cases (Sequential queries)
    struct timex tx2;
    ltm_memset(&tx2, 0, sizeof(tx2));
    ret = sys_clock_adjtime(0, &tx2);
    TEST_ASSERT(ret == 0, "clock_adjtime repeated call failed");

    // Case C: Error / Errno Handling (NULL buffer & invalid clockid)
    ret = sys_clock_adjtime(0, NULL);
    TEST_ASSERT(ret == -EFAULT, "clock_adjtime(NULL) must return -EFAULT");

    ret = sys_clock_adjtime(999, &tx);
    TEST_ASSERT(ret == -EINVAL, "clock_adjtime(invalid clock) must return -EINVAL");

    TEST_PASS("test_sys_266_clock_adjtime passed!");
    return 0;
}
