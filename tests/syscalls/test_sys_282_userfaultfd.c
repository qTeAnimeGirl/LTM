#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Create default userfaultfd)
    int uffd = sys_userfaultfd(0);
    TEST_ASSERT(uffd >= 0, "userfaultfd(0) failed");

    // Case B: Boundary / Edge Cases (Create with CLOEXEC and NONBLOCK)
    int uffd2 = sys_userfaultfd(0x080000 | 0x000800);
    TEST_ASSERT(uffd2 >= 0, "userfaultfd(CLOEXEC|NONBLOCK) failed");

    // Case C: Error / Errno Handling (Invalid flags)
    int ret = sys_userfaultfd(0xFF0000);
    TEST_ASSERT(ret == -EINVAL, "userfaultfd(invalid flags) must return -EINVAL");

    sys_close(uffd);
    sys_close(uffd2);

    TEST_PASS("test_sys_282_userfaultfd passed!");
    return 0;
}
