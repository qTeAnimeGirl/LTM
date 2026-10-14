#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (query rseq support returns -ENOSYS)
    int ret = sys_rseq(NULL, 0, 0, 0);
    TEST_ASSERT(ret == -ENOSYS, "rseq must return -ENOSYS");

    // Case B: Boundary / Edge Cases
    ret = sys_rseq(NULL, 32, 0, 0);
    TEST_ASSERT(ret == -ENOSYS, "rseq(32) must return -ENOSYS");

    // Case C: Error / Errno Handling
    ret = sys_rseq((void *)0x1000, 32, 1, 0x1234);
    TEST_ASSERT(ret == -ENOSYS, "rseq(invalid) must return -ENOSYS");

    TEST_PASS("test_sys_293_rseq passed!");
    return 0;
}
