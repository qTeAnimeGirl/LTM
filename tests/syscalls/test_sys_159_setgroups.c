#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int list[1] = {0};

    // Case A: Happy Path
    int ret = sys_setgroups(1, list);
    TEST_ASSERT(ret == 0, "setgroups failed");

    // Case B: Boundary / Edge Cases (clear groups)
    ret = sys_setgroups(0, NULL);
    TEST_ASSERT(ret == 0, "setgroups(0, NULL) failed");

    // Case C: Error / Errno Handling
    ret = sys_setgroups(-1, list);
    TEST_ASSERT(ret == -EINVAL, "setgroups(-1) must return -EINVAL");
    ret = sys_setgroups(5, NULL);
    TEST_ASSERT(ret == -EFAULT, "setgroups(5, NULL) must return -EFAULT");

    TEST_PASS("test_sys_159_setgroups passed!");
    return 0;
}
