#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ppid = sys_getppid();
    TEST_ASSERT(ppid >= 0, "getppid returned negative");

    // Case B: Boundary / Edge Cases (repeated call)
    int ppid2 = sys_getppid();
    TEST_ASSERT(ppid2 == ppid, "getppid returned different value");

    // Case C: Error / Errno Handling (infallible)
    TEST_ASSERT(ppid >= 0, "getppid sanity check");

    TEST_PASS("test_sys_173_getppid passed!");
    return 0;
}
