#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (query RLIMIT_NOFILE = 7)
    struct linux_rlimit old_lim;
    int ret = sys_prlimit64(0, 7 /* RLIMIT_NOFILE */, NULL, &old_lim);
    TEST_ASSERT(ret == 0, "prlimit64(RLIMIT_NOFILE) failed");
    TEST_ASSERT(old_lim.rlim_cur > 0, "rlim_cur must be > 0");

    // Case B: Boundary / Edge Cases (query RLIMIT_STACK = 3)
    ret = sys_prlimit64(0, 3 /* RLIMIT_STACK */, NULL, &old_lim);
    TEST_ASSERT(ret == 0, "prlimit64(RLIMIT_STACK) failed");

    // Case C: Error / Errno Handling
    ret = sys_prlimit64(-9999, 7, NULL, &old_lim);
    TEST_ASSERT(ret == -ESRCH, "prlimit64(-9999) must return -ESRCH");
    ret = sys_prlimit64(0, 9999, NULL, &old_lim);
    TEST_ASSERT(ret == -EINVAL, "prlimit64(invalid_res) must return -EINVAL");

    TEST_PASS("test_sys_261_prlimit64 passed!");
    return 0;
}
