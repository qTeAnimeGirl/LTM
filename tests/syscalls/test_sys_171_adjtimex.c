#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Valid timex query)
    struct timex tx;
    ltm_memset(&tx, 0, sizeof(tx));
    int ret = sys_adjtimex(&tx);
    TEST_ASSERT(ret == 0, "adjtimex failed");
    TEST_ASSERT(tx.tick > 0, "adjtimex tick must be > 0");

    // Case B: Boundary / Edge Cases (Sequential queries)
    struct timex tx2;
    ltm_memset(&tx2, 0, sizeof(tx2));
    ret = sys_adjtimex(&tx2);
    TEST_ASSERT(ret == 0, "adjtimex second call failed");

    // Case C: Error / Errno Handling (NULL pointer)
    ret = sys_adjtimex(NULL);
    TEST_ASSERT(ret == -EFAULT, "adjtimex(NULL) must return -EFAULT");

    TEST_PASS("test_sys_171_adjtimex passed!");
    return 0;
}
