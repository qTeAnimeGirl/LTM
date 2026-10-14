#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int futex_word = 10;

    // Case A: Happy Path (FUTEX_WAKE)
    int ret = sys_futex(&futex_word, 1 /* FUTEX_WAKE */, 1, NULL, NULL, 0);
    TEST_ASSERT(ret >= 0, "futex(WAKE) failed");

    // Case B: Boundary / Edge Cases (FUTEX_WAIT value mismatch returns -EAGAIN)
    ret = sys_futex(&futex_word, 0 /* FUTEX_WAIT */, 999 /* val mismatch */, NULL, NULL, 0);
    TEST_ASSERT(ret == -EAGAIN, "futex(WAIT mismatch) must return -EAGAIN");

    // Case C: Error / Errno Handling (NULL uaddr)
    ret = sys_futex(NULL, 0, 0, NULL, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "futex(NULL) must return -EFAULT");

    TEST_PASS("test_sys_098_futex passed!");
    return 0;
}
