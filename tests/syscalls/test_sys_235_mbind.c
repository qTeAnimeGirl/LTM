#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    void *ptr = (void *)sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(ptr != MAP_FAILED, "mmap failed");

    unsigned long nmask = 1;

    // Case A: Happy Path (mbind with MPOL_DEFAULT = 0)
    int ret = sys_mbind(ptr, 16384, 0 /* MPOL_DEFAULT */, NULL, 0, 0);
    TEST_ASSERT(ret == 0, "mbind(MPOL_DEFAULT) failed");

    // Case B: Boundary / Edge Cases (mbind with MPOL_BIND = 2 and nodemask)
    ret = sys_mbind(ptr, 16384, 2 /* MPOL_BIND */, &nmask, 1, 0);
    TEST_ASSERT(ret == 0, "mbind(MPOL_BIND) failed");

    // Case C: Error / Errno Handling (Unaligned addr -> -EINVAL, Invalid mode -> -EINVAL)
    ret = sys_mbind((char *)ptr + 1024, 16384, 0, NULL, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "mbind(unaligned) must return -EINVAL");

    ret = sys_mbind(ptr, 16384, 999, NULL, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "mbind(invalid mode) must return -EINVAL");

    sys_munmap(ptr, 16384);
    TEST_PASS("test_sys_235_mbind passed!");
    return 0;
}
