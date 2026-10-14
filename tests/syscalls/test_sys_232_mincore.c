#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    void *addr = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap failed");
    ltm_memset(addr, 0xaa, 16384);

    // Case A: Happy Path
    unsigned char vec[2];
    ltm_memset(vec, 0, sizeof(vec));
    int ret = sys_mincore(addr, 16384, vec);
    TEST_ASSERT(ret == 0, "mincore failed");

    // Case B: Boundary / Edge Cases (len=0)
    ret = sys_mincore(addr, 0, vec);
    TEST_ASSERT(ret == 0, "mincore(len=0) failed");

    // Case C: Error / Errno Translation
    ret = sys_mincore(addr, 16384, NULL);
    TEST_ASSERT(ret == -EFAULT, "mincore(NULL vec) must return -EFAULT");
    ret = sys_mincore((void *)((uintptr_t)addr + 1), 16384, vec);
    TEST_ASSERT(ret == -EINVAL, "mincore(unaligned) must return -EINVAL");

    sys_munmap(addr, 16384);
    TEST_PASS("test_sys_232_mincore passed!");
    return 0;
}
