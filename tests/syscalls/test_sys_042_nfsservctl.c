#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Obsolete syscall must return -ENOSYS)
    int ret = sys_nfsservctl(0, NULL, NULL);
    TEST_ASSERT(ret == -ENOSYS, "nfsservctl must return -ENOSYS");

    // Case B: Boundary / Edge Cases (Non-zero commands)
    ret = sys_nfsservctl(1, NULL, NULL);
    TEST_ASSERT(ret == -ENOSYS, "nfsservctl(1) must return -ENOSYS");

    // Case C: Error / Errno Handling (Valid pointers still return -ENOSYS)
    char dummy[16];
    ret = sys_nfsservctl(0, dummy, dummy);
    TEST_ASSERT(ret == -ENOSYS, "nfsservctl with pointers must return -ENOSYS");

    TEST_PASS("test_sys_042_nfsservctl passed!");
    return 0;
}
