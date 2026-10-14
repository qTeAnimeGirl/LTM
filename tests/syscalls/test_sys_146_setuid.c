#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setuid(0);
    TEST_ASSERT(ret == 0, "setuid(0) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_setuid(sys_getuid());
    TEST_ASSERT(ret == 0, "setuid(current) failed");

    // Case C: Error / Errno Handling
    ret = sys_setuid(-9999);
    TEST_ASSERT(ret == -EINVAL, "setuid(-9999) must return -EINVAL");

    TEST_PASS("test_sys_146_setuid passed!");
    return 0;
}
