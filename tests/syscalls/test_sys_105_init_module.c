#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char dummy_mod[64];
    ltm_memset(dummy_mod, 0, sizeof(dummy_mod));

    // Case A: Happy Path (Attempt init_module in container -> EPERM)
    int ret = sys_init_module(dummy_mod, sizeof(dummy_mod), "");
    TEST_ASSERT(ret == -EPERM, "init_module must return -EPERM");

    // Case B: Boundary / Edge Cases (init_module with len == 0 -> -EPERM)
    ret = sys_init_module(dummy_mod, 0, "");
    TEST_ASSERT(ret == -EPERM, "init_module(len == 0) must return -EPERM");

    // Case C: Error / Errno Handling (NULL module_image -> -EFAULT)
    ret = sys_init_module(NULL, sizeof(dummy_mod), "");
    TEST_ASSERT(ret == -EFAULT, "init_module(NULL) must return -EFAULT");

    TEST_PASS("test_sys_105_init_module passed!");
    return 0;
}
