#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char ctx[64];
    ltm_memset(ctx, 0, sizeof(ctx));

    // Case A: Happy Path (Set self attribute returns -EOPNOTSUPP on vanilla kernel)
    int ret = sys_lsm_set_self_attr(1, ctx, sizeof(ctx), 0);
    TEST_ASSERT(ret == -EOPNOTSUPP, "lsm_set_self_attr must return -EOPNOTSUPP");

    // Case B: Boundary / Edge Cases (Attribute 0)
    ret = sys_lsm_set_self_attr(0, ctx, sizeof(ctx), 0);
    TEST_ASSERT(ret == -EOPNOTSUPP, "lsm_set_self_attr(attr=0) must return -EOPNOTSUPP");

    // Case C: Error / Errno Handling (NULL ctx -> -EFAULT, bad flags -> -EINVAL)
    ret = sys_lsm_set_self_attr(1, NULL, sizeof(ctx), 0);
    TEST_ASSERT(ret == -EFAULT, "lsm_set_self_attr(NULL ctx) must return -EFAULT");

    ret = sys_lsm_set_self_attr(1, ctx, sizeof(ctx), 999);
    TEST_ASSERT(ret == -EINVAL, "lsm_set_self_attr(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_460_lsm_set_self_attr passed!");
    return 0;
}
