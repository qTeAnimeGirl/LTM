#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (query tv)
    struct linux_timeval tv;
    int ret = sys_gettimeofday(&tv, NULL);
    TEST_ASSERT(ret == 0, "gettimeofday failed");
    TEST_ASSERT(tv.tv_sec > 1000000000L, "gettimeofday sec sanity check");

    // Case B: Boundary / Edge Cases (query with NULL tv and NULL tz)
    ret = sys_gettimeofday(NULL, NULL);
    TEST_ASSERT(ret == 0, "gettimeofday(NULL, NULL) must succeed");

    // Case C: Error / Errno Handling (monotonic sanity check across repeated calls)
    struct linux_timeval tv2;
    ret = sys_gettimeofday(&tv2, NULL);
    TEST_ASSERT(ret == 0, "gettimeofday 2 failed");
    TEST_ASSERT(tv2.tv_sec >= tv.tv_sec, "time must not move backward");

    TEST_PASS("test_sys_169_gettimeofday passed!");
    return 0;
}
