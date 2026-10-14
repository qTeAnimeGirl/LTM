#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (sleep 1 ms)
    struct linux_timespec req = {0, 1000000};
    int ret = sys_nanosleep(&req, NULL);
    TEST_ASSERT(ret == 0, "nanosleep(1ms) failed");

    // Case B: Boundary / Edge Cases (sleep 0 ns)
    req.tv_sec = 0;
    req.tv_nsec = 0;
    ret = sys_nanosleep(&req, NULL);
    TEST_ASSERT(ret == 0, "nanosleep(0) failed");

    // Case C: Error / Errno Handling
    ret = sys_nanosleep(NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "nanosleep(NULL) must return -EFAULT");
    struct linux_timespec bad_req = {0, 2000000000L};
    ret = sys_nanosleep(&bad_req, NULL);
    TEST_ASSERT(ret == -EINVAL, "nanosleep(bad_nsec) must return -EINVAL");

    TEST_PASS("test_sys_101_nanosleep passed!");
    return 0;
}
