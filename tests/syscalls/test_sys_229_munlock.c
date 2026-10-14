#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    void *addr = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap failed");

    sys_mlock(addr, 16384);

    // Case A: Happy Path
    int ret = sys_munlock(addr, 16384);
    TEST_ASSERT(ret == 0, "munlock failed");

    // Case B: Boundary / Edge Cases (len=0)
    ret = sys_munlock(addr, 0);
    TEST_ASSERT(ret == 0, "munlock(len=0) failed");

    // Case C: Error / Errno Translation
    ret = sys_munlock((void *)((uintptr_t)addr + 1), 16384);
    TEST_ASSERT(ret == -EINVAL, "munlock(unaligned) must return -EINVAL");

    sys_munmap(addr, 16384);
    TEST_PASS("test_sys_229_munlock passed!");
    return 0;
}
