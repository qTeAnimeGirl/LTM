#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Allocate 16KB aligned anonymous page
    void *ptr = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT((long)ptr > 0, "mmap failed");

    // Case A: Happy Path (pkey_mprotect with pkey 0 to PROT_READ)
    int ret = sys_pkey_mprotect(ptr, 16384, PROT_READ, 0);
    TEST_ASSERT(ret == 0, "pkey_mprotect with pkey 0 failed");

    // Case B: Boundary / Edge Cases (pkey_mprotect back to PROT_READ | PROT_WRITE)
    ret = sys_pkey_mprotect(ptr, 16384, PROT_READ | PROT_WRITE, 0);
    TEST_ASSERT(ret == 0, "pkey_mprotect PROT_READ|WRITE failed");
    *(volatile char *)ptr = 'Z';
    TEST_ASSERT(*(volatile char *)ptr == 'Z', "memory write verification failed");

    ret = sys_munmap(ptr, 16384);
    TEST_ASSERT(ret == 0, "munmap failed");

    // Case C: Error / Errno Handling (negative pkey -> -EINVAL, NULL addr -> -EINVAL, len 0 -> -EINVAL)
    ret = sys_pkey_mprotect(ptr, 16384, PROT_READ, -1);
    TEST_ASSERT(ret == -EINVAL, "pkey_mprotect with negative pkey must return -EINVAL");

    ret = sys_pkey_mprotect(NULL, 16384, PROT_READ, 0);
    TEST_ASSERT(ret == -EINVAL, "pkey_mprotect with NULL addr must return -EINVAL");

    ret = sys_pkey_mprotect(ptr, 0, PROT_READ, 0);
    TEST_ASSERT(ret == -EINVAL, "pkey_mprotect with len 0 must return -EINVAL");

    TEST_PASS("test_sys_288_pkey_mprotect passed!");
    return 0;
}
