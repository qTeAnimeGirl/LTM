#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    aio_context_t ctx = 0;

    // Case A: Happy Path (Setup AIO context with 32 events)
    int ret = sys_io_setup(32, &ctx);
    TEST_ASSERT(ret == 0, "io_setup(32) failed");
    TEST_ASSERT(ctx != 0, "io_setup returned zero context");

    // Case B: Boundary / Edge Cases (Destroy and recreate with 1 event)
    sys_io_destroy(ctx);
    ctx = 0;
    ret = sys_io_setup(1, &ctx);
    TEST_ASSERT(ret == 0, "io_setup(1) failed");
    sys_io_destroy(ctx);

    // Case C: Error / Errno Handling (0 events -> EINVAL, NULL ctx -> EFAULT)
    ret = sys_io_setup(0, &ctx);
    TEST_ASSERT(ret == -EINVAL, "io_setup(0) must return -EINVAL");

    ret = sys_io_setup(32, NULL);
    TEST_ASSERT(ret == -EFAULT, "io_setup(NULL) must return -EFAULT");

    TEST_PASS("test_sys_000_io_setup passed!");
    return 0;
}
