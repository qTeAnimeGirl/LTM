#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int tid = sys_gettid();
    TEST_ASSERT(tid > 0, "gettid returned <= 0");

    // Case B: Boundary / Edge Cases
    int tid2 = sys_gettid();
    TEST_ASSERT(tid2 == tid, "gettid returned different value");

    // Case C: Error / Errno Handling (infallible)
    TEST_ASSERT(tid > 0, "gettid sanity check");

    TEST_PASS("test_sys_178_gettid passed!");
    return 0;
}
