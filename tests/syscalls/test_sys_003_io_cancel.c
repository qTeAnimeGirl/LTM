#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    aio_context_t ctx = 0;
    int ret = sys_io_setup(16, &ctx);
    TEST_ASSERT(ret == 0, "io_setup failed");

    struct iocb cb;
    ltm_memset(&cb, 0, sizeof(cb));
    struct io_event ev;
    ltm_memset(&ev, 0, sizeof(ev));

    // Case A: Happy Path (Attempt cancel on uncompleted request -> EAGAIN or success)
    ret = sys_io_cancel(ctx, &cb, &ev);
    TEST_ASSERT(ret == -EAGAIN || ret == 0, "io_cancel returned unexpected status");

    // Case B: Boundary / Edge Cases (Repeated cancel calls)
    ret = sys_io_cancel(ctx, &cb, &ev);
    TEST_ASSERT(ret == -EAGAIN || ret == 0, "Repeated io_cancel failed");

    // Case C: Error / Errno Handling (ctx 0, NULL iocb, NULL result -> EINVAL)
    ret = sys_io_cancel(0, &cb, &ev);
    TEST_ASSERT(ret == -EINVAL, "io_cancel(0 ctx) must return -EINVAL");

    ret = sys_io_cancel(ctx, NULL, &ev);
    TEST_ASSERT(ret == -EINVAL, "io_cancel(NULL iocb) must return -EINVAL");

    ret = sys_io_cancel(ctx, &cb, NULL);
    TEST_ASSERT(ret == -EINVAL, "io_cancel(NULL result) must return -EINVAL");

    sys_io_destroy(ctx);
    TEST_PASS("test_sys_003_io_cancel passed!");
    return 0;
}
