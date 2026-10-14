#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_sched_yield();
    TEST_ASSERT(ret == 0, "sched_yield failed");

    // Case B: Boundary / Edge Cases (repeated yields)
    for (int i = 0; i < 5; i++) {
        ret = sys_sched_yield();
        TEST_ASSERT(ret == 0, "repeated sched_yield failed");
    }

    // Case C: Register preservation
    register long r20 __asm__("x20") = 0xabcdef01;
    sys_sched_yield();
    TEST_ASSERT(r20 == 0xabcdef01, "sched_yield corrupted x20");

    TEST_PASS("test_sys_124_sched_yield passed!");
    return 0;
}
