#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setfsuid(0);
    TEST_ASSERT(ret == 0, "setfsuid(0) failed");

    // Case B: Boundary / Edge Cases (repeated call)
    ret = sys_setfsuid(0);
    TEST_ASSERT(ret == 0, "setfsuid(0) repeated failed");

    // Case C: Non-root request returns previous (0)
    ret = sys_setfsuid(1000);
    TEST_ASSERT(ret == 0, "setfsuid(1000) must return previous UID (0)");

    TEST_PASS("test_sys_151_setfsuid passed!");
    return 0;
}
