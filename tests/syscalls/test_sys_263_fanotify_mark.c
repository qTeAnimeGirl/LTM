#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt fanotify_mark on file -> -EPERM)
    int ret = sys_fanotify_mark(3, 1 /* FAN_MARK_ADD */, 0x01 /* FAN_ACCESS */, AT_FDCWD, ".");
    TEST_ASSERT(ret == -EPERM, "fanotify_mark must return -EPERM");

    // Case B: Boundary / Edge Cases (fanotify_mark with FAN_MARK_FLUSH = 0x80)
    ret = sys_fanotify_mark(3, 0x80 /* FAN_MARK_FLUSH */, 0, -1, NULL);
    TEST_ASSERT(ret == -EPERM, "fanotify_mark(FLUSH) must return -EPERM");

    // Case C: Error / Errno Handling (Invalid fanotify descriptor -> -EBADF)
    ret = sys_fanotify_mark(-1, 1, 0x01, AT_FDCWD, ".");
    TEST_ASSERT(ret == -EBADF, "fanotify_mark(-1) must return -EBADF");

    TEST_PASS("test_sys_263_fanotify_mark passed!");
    return 0;
}
