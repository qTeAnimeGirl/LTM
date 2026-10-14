#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

static char alt_stack[65536];

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (query current stack)
    linux_stack_t old_ss;
    int ret = sys_sigaltstack(NULL, &old_ss);
    TEST_ASSERT(ret == 0, "sigaltstack(query) failed");

    // Case B: Boundary / Edge Cases (set new stack)
    linux_stack_t new_ss;
    new_ss.ss_sp = alt_stack;
    new_ss.ss_flags = 0;
    new_ss.ss_size = sizeof(alt_stack);
    ret = sys_sigaltstack(&new_ss, &old_ss);
    TEST_ASSERT(ret == 0, "sigaltstack(set) failed");

    // Case C: Error / Errno Handling (stack size too small < 2048)
    new_ss.ss_size = 100;
    ret = sys_sigaltstack(&new_ss, NULL);
    TEST_ASSERT(ret == -ENOMEM || ret == -EINVAL, "sigaltstack(tiny_stack) must return -ENOMEM or -EINVAL");

    TEST_PASS("test_sys_132_sigaltstack passed!");
    return 0;
}
