#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Hardware shadow stack unsupported on standard ARM64 returns -ENOSYS)
    int ret = sys_map_shadow_stack(0, 16384, 0);
    TEST_ASSERT(ret == -ENOSYS, "map_shadow_stack must return -ENOSYS on standard ARM64");

    // Case B: Boundary / Edge Cases (Nonzero address)
    ret = sys_map_shadow_stack(0x100000000ULL, 16384, 0);
    TEST_ASSERT(ret == -ENOSYS, "map_shadow_stack(addr) must return -ENOSYS");

    // Case C: Error / Errno Handling (Shadow stack unsupported on Apple Silicon hardware)
    ret = sys_map_shadow_stack(0, 0, 1);
    TEST_ASSERT(ret == -ENOSYS, "map_shadow_stack must return -ENOSYS");

    TEST_PASS("test_sys_453_map_shadow_stack passed!");
    return 0;
}
