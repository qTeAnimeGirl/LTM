#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    aio_context_t ctx = 0;
    int ret = sys_io_setup(16, &ctx);
    TEST_ASSERT(ret == 0, "io_setup failed");

    // Case A: Happy Path (Destroy existing context)
    ret = sys_io_destroy(ctx);
    TEST_ASSERT(ret == 0, "io_destroy failed");

    // Case B: Boundary / Edge Cases (Recreate and destroy high event count)
    ctx = 0;
    ret = sys_io_setup(1024, &ctx);
    TEST_ASSERT(ret == 0, "io_setup(1024) failed");
    ret = sys_io_destroy(ctx);
    TEST_ASSERT(ret == 0, "io_destroy(1024) failed");

    // Case C: Error / Errno Handling (ctx 0 must return -EINVAL)
    ret = sys_io_destroy(0);
    TEST_ASSERT(ret == -EINVAL, "io_destroy(0) must return -EINVAL");

    TEST_PASS("test_sys_001_io_destroy passed!");
    return 0;
}
