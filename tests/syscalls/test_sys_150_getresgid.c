#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int r = -1, e = -1, s = -1;

    // Case A: Happy Path
    int ret = sys_getresgid(&r, &e, &s);
    TEST_ASSERT(ret == 0, "getresgid failed");
    TEST_ASSERT(r == 0 && e == 0 && s == 0, "getresgid expected root (0, 0, 0)");

    // Case B: Boundary / Edge Cases (repeated call)
    ret = sys_getresgid(&r, &e, &s);
    TEST_ASSERT(ret == 0, "getresgid repeat failed");

    // Case C: Error / Errno Handling
    ret = sys_getresgid(NULL, &e, &s);
    TEST_ASSERT(ret == -EFAULT, "getresgid(NULL) must return -EFAULT");

    TEST_PASS("test_sys_150_getresgid passed!");
    return 0;
}
