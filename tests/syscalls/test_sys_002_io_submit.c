#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    aio_context_t ctx = 0;
    int ret = sys_io_setup(16, &ctx);
    TEST_ASSERT(ret == 0, "io_setup failed");

    struct iocb cb;
    ltm_memset(&cb, 0, sizeof(cb));
    struct iocb *cbs[1] = { &cb };

    // Case A: Happy Path (Submit 1 iocb)
    ret = sys_io_submit(ctx, 1, cbs);
    TEST_ASSERT(ret >= 0, "io_submit(1) failed");

    // Case B: Boundary / Edge Cases (Submit 0 iocbs)
    ret = sys_io_submit(ctx, 0, NULL);
    TEST_ASSERT(ret == 0, "io_submit(0, NULL) must return 0");

    // Case C: Error / Errno Handling (ctx 0 -> EINVAL, nr < 0 -> EINVAL, NULL iocbpp -> EFAULT)
    ret = sys_io_submit(0, 1, cbs);
    TEST_ASSERT(ret == -EINVAL, "io_submit(0 ctx) must return -EINVAL");

    ret = sys_io_submit(ctx, -1, cbs);
    TEST_ASSERT(ret == -EINVAL, "io_submit(nr < 0) must return -EINVAL");

    ret = sys_io_submit(ctx, 1, NULL);
    TEST_ASSERT(ret == -EFAULT, "io_submit(1, NULL) must return -EFAULT");

    sys_io_destroy(ctx);
    TEST_PASS("test_sys_002_io_submit passed!");
    return 0;
}
