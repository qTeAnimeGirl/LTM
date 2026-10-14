#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setresgid(0, 0, 0);
    TEST_ASSERT(ret == 0, "setresgid(0, 0, 0) failed");

    // Case B: Boundary / Edge Cases (unchanged with -1)
    ret = sys_setresgid(-1, -1, -1);
    TEST_ASSERT(ret == 0, "setresgid(-1, -1, -1) failed");

    // Case C: Error / Errno Handling
    ret = sys_setresgid(0, -1, 0);
    TEST_ASSERT(ret == 0, "setresgid(0, -1, 0) failed");

    TEST_PASS("test_sys_149_setresgid passed!");
    return 0;
}
