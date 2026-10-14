#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setsid();
    TEST_ASSERT(ret > 0 || ret == -EPERM, "setsid() unexpected return");

    // Case B: Boundary / Edge Cases (repeated call)
    int ret2 = sys_setsid();
    TEST_ASSERT(ret2 == -EPERM, "repeated setsid must return -EPERM");

    // Case C: Verify sid valid
    int sid = sys_getsid(0);
    TEST_ASSERT(sid > 0, "getsid > 0");

    TEST_PASS("test_sys_157_setsid passed!");
    return 0;
}
