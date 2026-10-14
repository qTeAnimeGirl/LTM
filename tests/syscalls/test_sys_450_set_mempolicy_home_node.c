#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Home node 0 on single-node Darwin NUMA topology)
    int ret = sys_set_mempolicy_home_node(0x100000000ULL, 16384, 0, 0);
    TEST_ASSERT(ret == 0, "set_mempolicy_home_node failed");

    // Case B: Boundary / Edge Cases (Another range with home node 0)
    ret = sys_set_mempolicy_home_node(0x200000000ULL, 65536, 0, 0);
    TEST_ASSERT(ret == 0, "set_mempolicy_home_node range 2 failed");

    // Case C: Error / Errno Handling (Invalid home node > 0 -> -EINVAL, bad flags -> -EINVAL)
    ret = sys_set_mempolicy_home_node(0x100000000ULL, 16384, 1, 0);
    TEST_ASSERT(ret == -EINVAL, "set_mempolicy_home_node(node > 0) must return -EINVAL");

    ret = sys_set_mempolicy_home_node(0x100000000ULL, 16384, 0, 999);
    TEST_ASSERT(ret == -EINVAL, "set_mempolicy_home_node(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_450_set_mempolicy_home_node passed!");
    return 0;
}
