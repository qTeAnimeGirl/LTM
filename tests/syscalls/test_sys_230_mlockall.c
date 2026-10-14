#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (MCL_CURRENT = 1)
    int ret = sys_mlockall(1);
    TEST_ASSERT(ret == 0, "mlockall(MCL_CURRENT) failed");
    sys_munlockall();

    // Case B: Boundary / Edge Cases (MCL_CURRENT | MCL_FUTURE = 3)
    ret = sys_mlockall(3);
    TEST_ASSERT(ret == 0, "mlockall(3) failed");
    sys_munlockall();

    // Case C: Error / Errno Translation
    ret = sys_mlockall(0);
    TEST_ASSERT(ret == -EINVAL, "mlockall(0) must return -EINVAL");
    ret = sys_mlockall(0x80);
    TEST_ASSERT(ret == -EINVAL, "mlockall(invalid flags) must return -EINVAL");

    TEST_PASS("test_sys_230_mlockall passed!");
    return 0;
}
