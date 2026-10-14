#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt swapoff in container -> -EPERM)
    int ret = sys_swapoff("/dev/swapfile");
    TEST_ASSERT(ret == -EPERM, "swapoff must return -EPERM");

    // Case B: Boundary / Edge Cases (swapoff with root directory)
    ret = sys_swapoff("/");
    TEST_ASSERT(ret == -EPERM, "swapoff(/) must return -EPERM");

    // Case C: Error / Errno Handling (NULL path -> -EFAULT)
    ret = sys_swapoff(NULL);
    TEST_ASSERT(ret == -EFAULT, "swapoff(NULL) must return -EFAULT");

    TEST_PASS("test_sys_225_swapoff passed!");
    return 0;
}
