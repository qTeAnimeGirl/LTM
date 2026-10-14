#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    unsigned cpu = 999;
    unsigned node = 999;

    // Case A: Happy Path
    int ret = sys_getcpu(&cpu, &node, NULL);
    TEST_ASSERT(ret == 0, "getcpu(&cpu, &node, NULL) failed");
    TEST_ASSERT(cpu == 0, "cpu must be initialized to 0");
    TEST_ASSERT(node == 0, "node must be initialized to 0");

    // Case B: Boundary / Edge Cases (both NULL)
    ret = sys_getcpu(NULL, NULL, NULL);
    TEST_ASSERT(ret == 0, "getcpu(NULL, NULL, NULL) failed");

    // Case C: Single pointer checks
    cpu = 888;
    ret = sys_getcpu(&cpu, NULL, NULL);
    TEST_ASSERT(ret == 0, "getcpu(&cpu, NULL, NULL) failed");
    TEST_ASSERT(cpu == 0, "cpu must be 0");

    node = 888;
    ret = sys_getcpu(NULL, &node, NULL);
    TEST_ASSERT(ret == 0, "getcpu(NULL, &node, NULL) failed");
    TEST_ASSERT(node == 0, "node must be 0");

    TEST_PASS("test_sys_168_getcpu passed!");
    return 0;
}
