#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    void *addr = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap failed");

    // Case A: Happy Path (Standard mlock2)
    int ret = sys_mlock2(addr, 16384, 0);
    TEST_ASSERT(ret == 0, "mlock2(0) failed");

    // Case B: Boundary / Edge Cases (mlock2 with MLOCK_ONFAULT)
    ret = sys_mlock2(addr, 16384, 1 /* MLOCK_ONFAULT */);
    TEST_ASSERT(ret == 0, "mlock2(MLOCK_ONFAULT) failed");

    // Case C: Error / Errno Handling (Invalid flags & unaligned address)
    ret = sys_mlock2(addr, 16384, 0xFF);
    TEST_ASSERT(ret == -EINVAL, "mlock2(invalid flags) must return -EINVAL");

    ret = sys_mlock2((void *)((uintptr_t)addr + 123), 16384, 0);
    TEST_ASSERT(ret == -EINVAL, "mlock2(unaligned) must return -EINVAL");

    sys_munlock(addr, 16384);
    sys_munmap(addr, 16384);

    TEST_PASS("test_sys_284_mlock2 passed!");
    return 0;
}
