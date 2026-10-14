#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (clear ITIMER_REAL)
    struct linux_itimerval zero, old;
    memset(&zero, 0, sizeof(zero));
    memset(&old, 0, sizeof(old));

    int ret = sys_setitimer(0 /* ITIMER_REAL */, &zero, &old);
    TEST_ASSERT(ret == 0, "setitimer(clear) failed");

    // Case B: Boundary / Edge Cases (set 10s then immediately cancel)
    struct linux_itimerval ten = {{0, 0}, {10, 0}};
    ret = sys_setitimer(0, &ten, &old);
    TEST_ASSERT(ret == 0, "setitimer(set) failed");
    ret = sys_setitimer(0, &zero, NULL);
    TEST_ASSERT(ret == 0, "setitimer(cancel) failed");

    // Case C: Error / Errno Handling
    ret = sys_setitimer(999, &zero, NULL);
    TEST_ASSERT(ret == -EINVAL, "setitimer(invalid_which) must return -EINVAL");
    ret = sys_setitimer(0, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "setitimer(NULL) must return -EFAULT");

    TEST_PASS("test_sys_103_setitimer passed!");
    return 0;
}
