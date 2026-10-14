#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (setting PER_LINUX = 0)
    int ret = sys_personality(0);
    TEST_ASSERT(ret == 0, "personality(0) must succeed");

    // Case B: Boundary / Edge Cases (query personality via 0xffffffff)
    ret = sys_personality(0xffffffffUL);
    TEST_ASSERT(ret == 0, "personality(0xffffffff) must return current persona (0)");

    // Case C: Setting alternate personality flags
    ret = sys_personality(1);
    TEST_ASSERT(ret == 0, "personality(1) must succeed");

    TEST_PASS("test_sys_092_personality passed!");
    return 0;
}
