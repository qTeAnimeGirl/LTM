#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Turn off process accounting via NULL filename)
    int ret = sys_acct(NULL);
    TEST_ASSERT(ret == 0 || ret == -EPERM, "acct(NULL) returned unexpected code");

    // Case B: Boundary / Edge Cases (Turn off accounting repeatedly)
    ret = sys_acct(NULL);
    TEST_ASSERT(ret == 0 || ret == -EPERM, "Repeated acct(NULL) failed");

    // Case C: Error / Errno Handling (Non-existent path -> -ENOENT or -EPERM)
    ret = sys_acct("/nonexistent_acct_file");
    TEST_ASSERT(ret == -ENOENT || ret == -EPERM, "acct(nonexistent) returned unexpected code");

    TEST_PASS("test_sys_089_acct passed!");
    return 0;
}
