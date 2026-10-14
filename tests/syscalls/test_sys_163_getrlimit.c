#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_rlimit rlim;
    ltm_memset(&rlim, 0, sizeof(rlim));

    // Case A: Happy Path (RLIMIT_NOFILE = 7)
    int ret = sys_getrlimit(7, &rlim);
    TEST_ASSERT(ret == 0, "getrlimit(RLIMIT_NOFILE) failed");
    TEST_ASSERT(rlim.rlim_cur > 0, "rlim_cur must be > 0");

    // Case B: Boundary / Edge Cases (RLIMIT_STACK = 3)
    ret = sys_getrlimit(3, &rlim);
    TEST_ASSERT(ret == 0, "getrlimit(RLIMIT_STACK) failed");
    TEST_ASSERT(rlim.rlim_cur > 0, "rlim_cur must be > 0");

    // Case C: Error / Errno Translation
    ret = sys_getrlimit(-1, &rlim);
    TEST_ASSERT(ret == -EINVAL, "getrlimit(-1) must return -EINVAL");
    ret = sys_getrlimit(7, NULL);
    TEST_ASSERT(ret == -EFAULT, "getrlimit(NULL) must return -EFAULT");

    TEST_PASS("test_sys_163_getrlimit passed!");
    return 0;
}
