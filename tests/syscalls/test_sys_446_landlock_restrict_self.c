#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Calling on non-ruleset descriptor returns -EBADF)
    int ret = sys_landlock_restrict_self(1, 0);
    TEST_ASSERT(ret == -EBADF, "landlock_restrict_self on non-ruleset fd must return -EBADF");

    // Case B: Boundary / Edge Cases (fd=2)
    ret = sys_landlock_restrict_self(2, 0);
    TEST_ASSERT(ret == -EBADF, "landlock_restrict_self(fd=2) must return -EBADF");

    // Case C: Error / Errno Handling (Negative fd -> -EBADF, bad flags -> -EINVAL)
    ret = sys_landlock_restrict_self(-1, 0);
    TEST_ASSERT(ret == -EBADF, "landlock_restrict_self(-1) must return -EBADF");

    ret = sys_landlock_restrict_self(1, 999);
    TEST_ASSERT(ret == -EINVAL, "landlock_restrict_self(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_446_landlock_restrict_self passed!");
    return 0;
}
