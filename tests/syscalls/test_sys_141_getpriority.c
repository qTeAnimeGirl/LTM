#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_getpriority(0 /* PRIO_PROCESS */, 0);
    TEST_ASSERT(ret >= 1 && ret <= 40, "getpriority returned out-of-range value");

    // Case B: Boundary / Edge Cases (explicit pid)
    ret = sys_getpriority(0, sys_getpid());
    TEST_ASSERT(ret >= 1 && ret <= 40, "getpriority(getpid) out of range");

    // Case C: Error / Errno Translation
    ret = sys_getpriority(-1, 0);
    TEST_ASSERT(ret == -EINVAL, "getpriority(which=-1) must return -EINVAL");
    ret = sys_getpriority(0, 999999);
    TEST_ASSERT(ret == -ESRCH, "getpriority(nonexistent pid) must return -ESRCH");

    TEST_PASS("test_sys_141_getpriority passed!");
    return 0;
}
