#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    uint64_t set = 0xdeadbeef;
    int ret = sys_rt_sigpending(&set, 8);
    TEST_ASSERT(ret == 0, "rt_sigpending failed");

    // Case B: Boundary / Edge Cases (repeated call)
    uint64_t set2 = 0;
    ret = sys_rt_sigpending(&set2, 8);
    TEST_ASSERT(ret == 0, "rt_sigpending repeated call failed");

    // Case C: Error / Errno Translation
    ret = sys_rt_sigpending(NULL, 8);
    TEST_ASSERT(ret == -EFAULT, "rt_sigpending(NULL) must return -EFAULT");
    ret = sys_rt_sigpending(&set, 4);
    TEST_ASSERT(ret == -EINVAL, "rt_sigpending(sigsetsize != 8) must return -EINVAL");

    TEST_PASS("test_sys_136_rt_sigpending passed!");
    return 0;
}
