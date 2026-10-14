#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    size_t sz = 64 * 1024;
    void *addr = sys_mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap failed");

    // Case A: Happy Path
    int ret = sys_madvise(addr, sz, MADV_NORMAL);
    TEST_ASSERT(ret == 0, "madvise(NORMAL) failed");
    ret = sys_madvise(addr, sz, MADV_WILLNEED);
    TEST_ASSERT(ret == 0, "madvise(WILLNEED) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_madvise(addr, sz, MADV_DONTNEED);
    TEST_ASSERT(ret == 0, "madvise(DONTNEED) failed");

    // Case C: Error / Errno Handling
    ret = sys_madvise(NULL, sz, MADV_NORMAL);
    TEST_ASSERT(ret == -EINVAL || ret == -ENOMEM, "madvise(NULL) must return -EINVAL or -ENOMEM");
    ret = sys_madvise(addr, sz, -999);
    TEST_ASSERT(ret == -EINVAL, "madvise(invalid_advice) must return -EINVAL");

    sys_munmap(addr, sz);
    TEST_PASS("test_sys_233_madvise passed!");
    return 0;
}
