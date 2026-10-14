#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    void *addr = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap failed");

    // Case A: Happy Path (MS_SYNC = 4)
    int ret = sys_msync(addr, 16384, 4);
    TEST_ASSERT(ret == 0, "msync(MS_SYNC) failed");

    // Case B: Boundary / Edge Cases (len=0)
    ret = sys_msync(addr, 0, 4);
    TEST_ASSERT(ret == 0, "msync(len=0) failed");

    // Case C: Error / Errno Translation
    ret = sys_msync((void *)((uintptr_t)addr + 1), 16384, 4);
    TEST_ASSERT(ret == -EINVAL, "msync(unaligned) must return -EINVAL");

    sys_munmap(addr, 16384);
    TEST_PASS("test_sys_227_msync passed!");
    return 0;
}
