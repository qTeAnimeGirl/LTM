#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (fspick returns -EPERM in sandbox)
    int ret = sys_fspick(AT_FDCWD, "/tmp", 0);
    TEST_ASSERT(ret == -EPERM, "fspick must return -EPERM in sandbox");

    // Case B: Boundary / Edge Cases (Root directory)
    ret = sys_fspick(AT_FDCWD, "/", 0);
    TEST_ASSERT(ret == -EPERM, "fspick(/) must return -EPERM");

    // Case C: Error / Errno Handling (NULL path -> -EFAULT)
    ret = sys_fspick(AT_FDCWD, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "fspick(NULL) must return -EFAULT");

    TEST_PASS("test_sys_433_fspick passed!");
    return 0;
}
