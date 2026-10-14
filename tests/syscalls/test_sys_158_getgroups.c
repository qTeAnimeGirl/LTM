#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (query count)
    int count = sys_getgroups(0, NULL);
    TEST_ASSERT(count >= 1, "getgroups(0, NULL) returned < 1");

    // Case B: Boundary / Edge Cases (fetch groups)
    int list[32];
    int ret = sys_getgroups(32, list);
    TEST_ASSERT(ret == count, "getgroups returned mismatched count");

    // Case C: Error / Errno Handling (invalid size or NULL)
    ret = sys_getgroups(-1, list);
    TEST_ASSERT(ret == -EINVAL, "getgroups(size=-1) must return -EINVAL");
    ret = sys_getgroups(10, NULL);
    TEST_ASSERT(ret == -EFAULT, "getgroups(size>0, NULL) must return -EFAULT");

    TEST_PASS("test_sys_158_getgroups passed!");
    return 0;
}
