#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Allocate 16KB-aligned memory
    void *ptr = (void *)sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(ptr != MAP_FAILED, "mmap failed");

    // Case A: Happy Path (16KB aligned remap_file_pages succeeds)
    int ret = sys_remap_file_pages(ptr, 16384, 0, 0, 0);
    TEST_ASSERT(ret == 0, "remap_file_pages failed");

    // Case B: Boundary / Edge Cases (size 0 -> -EINVAL)
    ret = sys_remap_file_pages(ptr, 0, 0, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "remap_file_pages(size 0) must return -EINVAL");

    // Case C: Error / Errno Handling (Unaligned address -> -EINVAL)
    ret = sys_remap_file_pages((char *)ptr + 1024, 16384, 0, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "remap_file_pages(unaligned) must return -EINVAL");

    sys_munmap(ptr, 16384);
    TEST_PASS("test_sys_234_remap_file_pages passed!");
    return 0;
}
