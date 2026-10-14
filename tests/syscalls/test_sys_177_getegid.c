#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (getegid == 0 root)
    int egid = sys_getegid();
    TEST_ASSERT(egid == 0, "getegid must return 0 (root)");

    // Case B: Boundary / Edge Cases
    int egid2 = sys_getegid();
    TEST_ASSERT(egid2 == egid, "getegid returned different value");

    // Case C: Error / Errno Handling (infallible)
    TEST_ASSERT(egid == 0, "getegid sanity check");

    TEST_PASS("test_sys_177_getegid passed!");
    return 0;
}
