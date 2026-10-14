#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char ctx[64];
    size_t size = sizeof(ctx);

    // Case A: Happy Path (Query self attribute returns -EOPNOTSUPP on vanilla kernel)
    int ret = sys_lsm_get_self_attr(1, ctx, &size, 0);
    TEST_ASSERT(ret == -EOPNOTSUPP, "lsm_get_self_attr must return -EOPNOTSUPP");

    // Case B: Boundary / Edge Cases (Attribute 0)
    ret = sys_lsm_get_self_attr(0, ctx, &size, 0);
    TEST_ASSERT(ret == -EOPNOTSUPP, "lsm_get_self_attr(attr=0) must return -EOPNOTSUPP");

    // Case C: Error / Errno Handling (NULL size -> -EFAULT, bad flags -> -EINVAL)
    ret = sys_lsm_get_self_attr(1, ctx, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "lsm_get_self_attr(NULL size) must return -EFAULT");

    ret = sys_lsm_get_self_attr(1, ctx, &size, 999);
    TEST_ASSERT(ret == -EINVAL, "lsm_get_self_attr(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_459_lsm_get_self_attr passed!");
    return 0;
}
