#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt swapon in container -> -EPERM)
    int ret = sys_swapon("/dev/swapfile", 0);
    TEST_ASSERT(ret == -EPERM, "swapon must return -EPERM");

    // Case B: Boundary / Edge Cases (swapon with swapflags 0x7fff)
    ret = sys_swapon("/dev/swapfile", 0x7fff);
    TEST_ASSERT(ret == -EPERM, "swapon with flags must return -EPERM");

    // Case C: Error / Errno Handling (NULL path -> -EFAULT)
    ret = sys_swapon(NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "swapon(NULL) must return -EFAULT");

    TEST_PASS("test_sys_224_swapon passed!");
    return 0;
}
