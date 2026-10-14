#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Valid timeval)
    struct timeval tv;
    tv.tv_sec = 1700000000;
    tv.tv_usec = 123456;
    int ret = sys_settimeofday(&tv, NULL);
    TEST_ASSERT(ret == 0, "settimeofday(&tv, NULL) failed");

    // Case B: Boundary / Edge Cases (NULL tv and tz is valid in Linux)
    ret = sys_settimeofday(NULL, NULL);
    TEST_ASSERT(ret == 0, "settimeofday(NULL, NULL) failed");

    // Case C: Error / Errno Handling (Invalid usec)
    tv.tv_usec = 1000000;
    ret = sys_settimeofday(&tv, NULL);
    TEST_ASSERT(ret == -EINVAL, "settimeofday(usec >= 1e6) must return -EINVAL");

    tv.tv_usec = -1;
    ret = sys_settimeofday(&tv, NULL);
    TEST_ASSERT(ret == -EINVAL, "settimeofday(usec < 0) must return -EINVAL");

    TEST_PASS("test_sys_170_settimeofday passed!");
    return 0;
}
