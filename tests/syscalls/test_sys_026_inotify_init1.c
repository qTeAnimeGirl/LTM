#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Standard flags)
    int fd = sys_inotify_init1(IN_CLOEXEC);
    TEST_ASSERT(fd >= 0, "inotify_init1(IN_CLOEXEC) failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases (Zero flags & NONBLOCK)
    fd = sys_inotify_init1(0);
    TEST_ASSERT(fd >= 0, "inotify_init1(0) failed");
    sys_close(fd);

    fd = sys_inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    TEST_ASSERT(fd >= 0, "inotify_init1(NONBLOCK|CLOEXEC) failed");
    sys_close(fd);

    // Case C: Error / Errno Translation (Invalid flags)
    int ret = sys_inotify_init1(0x12345678);
    TEST_ASSERT(ret == -EINVAL, "inotify_init1(invalid flags) must return -EINVAL");

    TEST_PASS("test_sys_026_inotify_init1 passed!");
    return 0;
}
