#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    char buf[1024];

    // Case A: Happy Path
    int ret = sys_getcwd(buf, sizeof(buf));
    TEST_ASSERT(ret > 0, "getcwd failed");
    TEST_ASSERT(buf[0] == '/', "getcwd buffer must start with '/'");

    // Case B: Boundary / Edge Cases (root directory)
    char root_buf[8];
    ret = sys_getcwd(root_buf, sizeof(root_buf));
    TEST_ASSERT(ret > 0, "getcwd with small buffer failed");

    // Case C: Error / Errno Handling
    ret = sys_getcwd(NULL, 1024);
    TEST_ASSERT(ret == -EINVAL || ret == -EFAULT, "getcwd(NULL) must return -EINVAL or -EFAULT");
    ret = sys_getcwd(buf, 0);
    TEST_ASSERT(ret == -EINVAL, "getcwd(size=0) must return -EINVAL");

    TEST_PASS("test_sys_017_getcwd passed!");
    return 0;
}
