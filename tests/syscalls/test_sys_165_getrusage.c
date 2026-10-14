#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_rusage ru;
    ltm_memset(&ru, 0, sizeof(ru));

    // Case A: Happy Path
    int ret = sys_getrusage(RUSAGE_SELF, &ru);
    TEST_ASSERT(ret == 0, "getrusage(RUSAGE_SELF) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_getrusage(RUSAGE_CHILDREN, &ru);
    TEST_ASSERT(ret == 0, "getrusage(RUSAGE_CHILDREN) failed");

    // Case C: Error / Errno Handling
    ret = sys_getrusage(99999, &ru);
    TEST_ASSERT(ret == -EINVAL, "getrusage(invalid_who) must return -EINVAL");
    ret = sys_getrusage(RUSAGE_SELF, NULL);
    TEST_ASSERT(ret == -EFAULT, "getrusage(NULL) must return -EFAULT");

    TEST_PASS("test_sys_165_getrusage passed!");
    return 0;
}
