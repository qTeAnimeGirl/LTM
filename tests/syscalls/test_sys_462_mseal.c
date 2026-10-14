#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Allocate 16KB aligned memory page
    void *ptr = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT((long)ptr > 0, "mmap failed");

    // Case A: Happy Path (Seal 16KB aligned memory range)
    int ret = sys_mseal((unsigned long)ptr, 16384, 0);
    TEST_ASSERT(ret == 0, "mseal failed");

    // Case B: Boundary / Edge Cases (Seal entire range again)
    ret = sys_mseal((unsigned long)ptr, 16384, 0);
    TEST_ASSERT(ret == 0, "mseal repeated call failed");

    sys_munmap(ptr, 16384);

    // Case C: Error / Errno Handling (Unaligned start -> -EINVAL, len=0 -> -EINVAL, bad flags -> -EINVAL)
    ret = sys_mseal(0x1234, 16384, 0);
    TEST_ASSERT(ret == -EINVAL, "mseal(unaligned) must return -EINVAL");

    ret = sys_mseal((unsigned long)ptr, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "mseal(len=0) must return -EINVAL");

    ret = sys_mseal((unsigned long)ptr, 16384, 999);
    TEST_ASSERT(ret == -EINVAL, "mseal(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_462_mseal passed!");
    return 0;
}
