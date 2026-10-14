#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int old_mask = sys_umask(022);
    TEST_ASSERT(old_mask >= 0, "umask(022) failed");

    // Case B: Boundary / Edge Cases
    int prev = sys_umask(0777);
    TEST_ASSERT(prev == 022, "umask(0777) returned wrong mask");
    sys_umask(old_mask);

    // Case C: Error / Upper bits masked
    int upper = sys_umask(077777);
    TEST_ASSERT((upper & ~0777) == 0, "umask returned upper bits");
    sys_umask(old_mask);

    TEST_PASS("test_sys_166_umask passed!");
    return 0;
}
