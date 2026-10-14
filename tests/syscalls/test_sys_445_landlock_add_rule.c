#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char dummy_rule[32];
    ltm_memset(dummy_rule, 0, sizeof(dummy_rule));

    // Case A: Happy Path (Calling on non-ruleset descriptor returns -EBADF)
    int ret = sys_landlock_add_rule(1, 1, dummy_rule, 0);
    TEST_ASSERT(ret == -EBADF, "landlock_add_rule on non-ruleset fd must return -EBADF");

    // Case B: Boundary / Edge Cases (Alternative rule type)
    ret = sys_landlock_add_rule(2, 2, dummy_rule, 0);
    TEST_ASSERT(ret == -EBADF, "landlock_add_rule(fd=2) must return -EBADF");

    // Case C: Error / Errno Handling (Negative fd -> -EBADF, NULL rule -> -EFAULT)
    ret = sys_landlock_add_rule(-1, 1, dummy_rule, 0);
    TEST_ASSERT(ret == -EBADF, "landlock_add_rule(-1) must return -EBADF");

    ret = sys_landlock_add_rule(1, 1, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "landlock_add_rule(NULL) must return -EFAULT");

    TEST_PASS("test_sys_445_landlock_add_rule passed!");
    return 0;
}
