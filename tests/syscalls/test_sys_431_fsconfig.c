#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Calling fsconfig on descriptor returns -EPERM in sandbox)
    int ret = sys_fsconfig(1, 0, "source", "/dev/sda1", 0);
    TEST_ASSERT(ret == -EPERM, "fsconfig must return -EPERM in sandbox");

    // Case B: Boundary / Edge Cases (Command 1)
    ret = sys_fsconfig(2, 1, "ro", NULL, 0);
    TEST_ASSERT(ret == -EPERM, "fsconfig(cmd=1) must return -EPERM");

    // Case C: Error / Errno Handling (Bad negative fd -> -EBADF)
    ret = sys_fsconfig(-1, 0, "source", "/dev/sda1", 0);
    TEST_ASSERT(ret == -EBADF, "fsconfig(-1) must return -EBADF");

    TEST_PASS("test_sys_431_fsconfig passed!");
    return 0;
}
