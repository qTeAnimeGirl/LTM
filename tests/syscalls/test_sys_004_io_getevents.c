#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    aio_context_t ctx = 0;
    int ret = sys_io_setup(16, &ctx);
    TEST_ASSERT(ret == 0, "io_setup failed");

    struct io_event events[4];
    ltm_memset(events, 0, sizeof(events));
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 0 };

    // Case A: Happy Path (Poll completed events with timeout 0)
    ret = sys_io_getevents(ctx, 0, 4, events, &ts);
    TEST_ASSERT(ret >= 0, "io_getevents failed");

    // Case B: Boundary / Edge Cases (min_nr == 0, nr == 0, NULL timeout)
    ret = sys_io_getevents(ctx, 0, 0, NULL, NULL);
    TEST_ASSERT(ret == 0, "io_getevents(0, 0) must return 0");

    // Case C: Error / Errno Handling (ctx 0, min_nr > nr, nr > 0 with NULL events)
    ret = sys_io_getevents(0, 0, 4, events, &ts);
    TEST_ASSERT(ret == -EINVAL, "io_getevents(0 ctx) must return -EINVAL");

    ret = sys_io_getevents(ctx, 5, 4, events, &ts);
    TEST_ASSERT(ret == -EINVAL, "io_getevents(min_nr > nr) must return -EINVAL");

    ret = sys_io_getevents(ctx, 1, 4, NULL, &ts);
    TEST_ASSERT(ret == -EFAULT, "io_getevents(NULL events) must return -EFAULT");

    sys_io_destroy(ctx);
    TEST_PASS("test_sys_004_io_getevents passed!");
    return 0;
}
