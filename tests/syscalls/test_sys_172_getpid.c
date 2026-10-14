#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int pid = sys_getpid();
    TEST_ASSERT(pid > 0, "getpid > 0");

    // Case B: Boundary / Consistency
    int pid2 = sys_getpid();
    TEST_ASSERT(pid == pid2, "getpid consistent");

    // Case C: Register preservation
    register long r21 __asm__("x21") = 0x99887766;
    sys_getpid();
    TEST_ASSERT(r21 == 0x99887766, "getpid corrupted x21");

    TEST_PASS("test_sys_172_getpid passed!");
    return 0;
}
