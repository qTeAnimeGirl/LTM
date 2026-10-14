#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt delete_module in container -> EPERM)
    int ret = sys_delete_module("dummy_module", 0);
    TEST_ASSERT(ret == -EPERM, "delete_module must return -EPERM");

    // Case B: Boundary / Edge Cases (delete_module with O_NONBLOCK=0x800 -> -EPERM)
    ret = sys_delete_module("dummy_module", 0x800);
    TEST_ASSERT(ret == -EPERM, "delete_module with flags must return -EPERM");

    // Case C: Error / Errno Handling (NULL name -> -EFAULT)
    ret = sys_delete_module(NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "delete_module(NULL) must return -EFAULT");

    TEST_PASS("test_sys_106_delete_module passed!");
    return 0;
}
