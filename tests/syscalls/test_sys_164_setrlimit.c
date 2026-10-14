#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_rlimit rlim;
    ltm_memset(&rlim, 0, sizeof(rlim));
    int ret = sys_getrlimit(7, &rlim);
    TEST_ASSERT(ret == 0, "getrlimit failed");

    // Case A: Happy Path
    ret = sys_setrlimit(7, &rlim);
    TEST_ASSERT(ret == 0, "setrlimit(RLIMIT_NOFILE) failed");

    // Case B: Boundary / Edge Cases (setting stack limit)
    ret = sys_setrlimit(3, &rlim);
    TEST_ASSERT(ret == 0, "setrlimit(RLIMIT_STACK) failed");

    // Case C: Error / Errno Translation
    ret = sys_setrlimit(-1, &rlim);
    TEST_ASSERT(ret == -EINVAL, "setrlimit(-1) must return -EINVAL");
    ret = sys_setrlimit(7, NULL);
    TEST_ASSERT(ret == -EFAULT, "setrlimit(NULL) must return -EFAULT");

    TEST_PASS("test_sys_164_setrlimit passed!");
    return 0;
}
