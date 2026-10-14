#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setfsgid(0);
    TEST_ASSERT(ret == 0, "setfsgid(0) failed");

    // Case B: Boundary / Edge Cases (repeated call)
    ret = sys_setfsgid(0);
    TEST_ASSERT(ret == 0, "setfsgid(0) repeated failed");

    // Case C: Non-root request returns previous (0)
    ret = sys_setfsgid(1000);
    TEST_ASSERT(ret == 0, "setfsgid(1000) must return previous GID (0)");

    TEST_PASS("test_sys_152_setfsgid passed!");
    return 0;
}
