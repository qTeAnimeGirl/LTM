#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int tid = 0;

    // Case A: Happy Path
    int ret = sys_set_tid_address(&tid);
    TEST_ASSERT(ret > 0, "set_tid_address failed to return tid");

    // Case B: Boundary / Edge Cases (repeated call)
    int ret2 = sys_set_tid_address(&tid);
    TEST_ASSERT(ret2 == ret, "set_tid_address returned different tid");

    // Case C: Error / Errno Handling (NULL pointer)
    int ret3 = sys_set_tid_address(NULL);
    TEST_ASSERT(ret3 == ret, "set_tid_address(NULL) must return tid");

    TEST_PASS("test_sys_096_set_tid_address passed!");
    return 0;
}
