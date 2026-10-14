#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    struct linux_tms t;
    ltm_memset(&t, 0, sizeof(t));

    // Case A: Happy Path
    long ret = sys_times(&t);
    TEST_ASSERT(ret >= 0, "times(&t) failed");

    // Case B: Boundary / Edge Cases (NULL allowed)
    long ret_null = sys_times(NULL);
    TEST_ASSERT(ret_null >= 0, "times(NULL) failed");

    // Case C: Error / Errno Handling
    ret = sys_times((struct linux_tms *)(void *)1);
    TEST_ASSERT(ret == -EFAULT, "times(bad_ptr) must return -EFAULT");

    TEST_PASS("test_sys_153_times passed!");
    return 0;
}
