#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt pivot_root inside container -> EPERM)
    int ret = sys_pivot_root("/mnt", "/mnt/old");
    TEST_ASSERT(ret == -EPERM, "pivot_root must return -EPERM");

    // Case B: Boundary / Edge Cases (pivot_root with same directory -> EPERM)
    ret = sys_pivot_root("/", "/");
    TEST_ASSERT(ret == -EPERM, "pivot_root(/, /) must return -EPERM");

    // Case C: Error / Errno Handling (NULL paths -> EFAULT)
    ret = sys_pivot_root(NULL, "/mnt/old");
    TEST_ASSERT(ret == -EFAULT, "pivot_root(NULL, ...) must return -EFAULT");

    ret = sys_pivot_root("/mnt", NULL);
    TEST_ASSERT(ret == -EFAULT, "pivot_root(..., NULL) must return -EFAULT");

    TEST_PASS("test_sys_041_pivot_root passed!");
    return 0;
}
