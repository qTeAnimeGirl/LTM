#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Valid path returns -EPERM for container sandbox security)
    int ret = sys_open_tree(AT_FDCWD, "/tmp", 0);
    TEST_ASSERT(ret == -EPERM, "open_tree must return -EPERM in sandbox");

    // Case B: Boundary / Edge Cases (Root directory path)
    ret = sys_open_tree(AT_FDCWD, "/", 0);
    TEST_ASSERT(ret == -EPERM, "open_tree(/) must return -EPERM");

    // Case C: Error / Errno Handling (NULL path -> -EFAULT)
    ret = sys_open_tree(AT_FDCWD, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "open_tree(NULL) must return -EFAULT");

    TEST_PASS("test_sys_428_open_tree passed!");
    return 0;
}
