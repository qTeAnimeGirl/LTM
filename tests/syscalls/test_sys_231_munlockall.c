#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    sys_mlockall(1);
    int ret = sys_munlockall();
    TEST_ASSERT(ret == 0, "munlockall failed");

    // Case B: Boundary / Edge Cases (repeated call)
    ret = sys_munlockall();
    TEST_ASSERT(ret == 0, "munlockall repeated call failed");

    // Case C: Calling when nothing was explicitly locked
    ret = sys_munlockall();
    TEST_ASSERT(ret == 0, "munlockall benign call failed");

    TEST_PASS("test_sys_231_munlockall passed!");
    return 0;
}
