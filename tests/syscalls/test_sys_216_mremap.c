#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (expand mapping)
    void *addr = sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap failed");
    ltm_memcpy(addr, "mremap_test_string", 18);

    void *new_addr = sys_mremap(addr, 16384, 32768, 1 /* MREMAP_MAYMOVE */, NULL);
    TEST_ASSERT((long)new_addr > 0, "mremap expand failed");
    TEST_ASSERT(ltm_memcmp(new_addr, "mremap_test_string", 18) == 0, "data corrupted after mremap");
    sys_munmap(new_addr, 32768);

    // Case B: Boundary / Edge Cases (shrink mapping)
    addr = sys_mmap(NULL, 32768, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap 32K failed");
    new_addr = sys_mremap(addr, 32768, 16384, 0, NULL);
    TEST_ASSERT((long)new_addr > 0, "mremap shrink failed");
    sys_munmap(new_addr, 16384);

    // Case C: Error / Errno Translation
    void *err = sys_mremap((void *)0x1001, 16384, 32768, 0, NULL);
    TEST_ASSERT((long)err == -EINVAL, "mremap unaligned must return -EINVAL");
    err = sys_mremap(addr, 0, 16384, 0, NULL);
    TEST_ASSERT((long)err == -EINVAL, "mremap(old_size=0) must return -EINVAL");

    TEST_PASS("test_sys_216_mremap passed!");
    return 0;
}
