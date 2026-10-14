#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Allocate 16KB page
    void *p = (void *)sys_mmap(NULL, 16384, 0x1 | 0x2 /* RW */, 0x20 | 0x02 /* ANON|PRIVATE */, -1, 0);
    TEST_ASSERT(p != (void *)-1, "mmap failed");

    // Case A: Happy Path (change to read-only, then back to read-write)
    int ret = sys_mprotect(p, 16384, 0x1 /* PROT_READ */);
    TEST_ASSERT(ret == 0, "mprotect(PROT_READ) failed");
    ret = sys_mprotect(p, 16384, 0x1 | 0x2 /* PROT_READ|PROT_WRITE */);
    TEST_ASSERT(ret == 0, "mprotect(PROT_READ|WRITE) failed");

    // Case B: Boundary / Edge Cases (PROT_NONE)
    ret = sys_mprotect(p, 16384, 0 /* PROT_NONE */);
    TEST_ASSERT(ret == 0, "mprotect(PROT_NONE) failed");
    sys_munmap(p, 16384);

    // Case C: Error / Errno Handling
    ret = sys_mprotect(NULL, 16384, 0x1);
    TEST_ASSERT(ret == -EINVAL, "mprotect(NULL) must return -EINVAL");
    ret = sys_mprotect(p, 0, 0x1);
    TEST_ASSERT(ret == -EINVAL, "mprotect(len=0) must return -EINVAL");

    TEST_PASS("test_sys_226_mprotect passed!");
    return 0;
}
