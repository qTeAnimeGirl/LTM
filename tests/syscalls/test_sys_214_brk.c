#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (query and expand)
    void *cur = sys_brk(NULL);
    TEST_ASSERT(cur != MAP_FAILED && cur != NULL, "sys_brk(0) failed");

    uintptr_t expand_sz = 64 * 1024;
    void *target = (void *)((uintptr_t)cur + expand_sz);
    void *new_brk = sys_brk(target);
    TEST_ASSERT(new_brk >= target, "sys_brk expand failed");

    // Case B: Boundary / Integrity
    uint32_t *u = (uint32_t *)cur;
    u[0] = 0x12345678;
    TEST_ASSERT(u[0] == 0x12345678, "brk memory write corrupted");

    // Case C: Shrink
    void *shrunk = sys_brk(cur);
    TEST_ASSERT(shrunk != MAP_FAILED, "sys_brk shrink failed");

    TEST_PASS("test_sys_214_brk passed!");
    return 0;
}
