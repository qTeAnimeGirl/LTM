#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    sys_sync();

    // Case B: Boundary / Edge Cases (sequential calls)
    sys_sync();
    sys_sync();

    // Case C: Register preservation test
    register long r19 __asm__("x19") = 0x12345678;
    sys_sync();
    TEST_ASSERT(r19 == 0x12345678, "sync corrupted callee-saved register x19");

    TEST_PASS("test_sys_081_sync passed!");
    return 0;
}
