#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    size_t sz = 64 * 1024;
    void *p = sys_mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(p != MAP_FAILED, "mmap failed");

    // Case A: Happy Path
    int ret = sys_munmap(p, sz);
    TEST_ASSERT(ret == 0, "munmap failed");

    // Case B: Boundary / Edge Cases (partial unmap)
    p = sys_mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(p != MAP_FAILED, "mmap 2 failed");
    ret = sys_munmap(p, 16384);
    TEST_ASSERT(ret == 0, "munmap partial failed");
    sys_munmap((void *)((uintptr_t)p + 16384), sz - 16384);

    // Case C: Error / Errno Handling
    ret = sys_munmap(NULL, sz);
    TEST_ASSERT(ret == -EINVAL, "munmap(NULL) must return -EINVAL");
    ret = sys_munmap(p, 0);
    TEST_ASSERT(ret == -EINVAL, "munmap(len=0) must return -EINVAL");

    TEST_PASS("test_sys_215_munmap passed!");
    return 0;
}
