#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    size_t sz = 64 * 1024;

    // Case A: Happy Path
    void *p = sys_mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(p != MAP_FAILED && p != NULL, "mmap failed");
    char *c = (char *)p;
    c[0] = 'M';
    c[sz - 1] = 'Z';
    TEST_ASSERT(c[0] == 'M' && c[sz - 1] == 'Z', "mmap write corrupted");
    sys_munmap(p, sz);

    // Case B: Boundary / Edge Cases (PROT_READ only)
    p = sys_mmap(NULL, 16384, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(p != MAP_FAILED, "mmap(PROT_READ) failed");
    sys_munmap(p, 16384);

    // Case C: Error / Errno Handling
    p = sys_mmap(NULL, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT((long)p == -EINVAL, "mmap(len=0) must return -EINVAL");

    TEST_PASS("test_sys_222_mmap passed!");
    return 0;
}
