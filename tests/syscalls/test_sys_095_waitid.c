#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (spawn child and waitid)
    long pid = sys_clone(17 /* SIGCHLD */, NULL, NULL, NULL, NULL);
    TEST_ASSERT(pid >= 0, "clone failed");
    if (pid == 0) {
        sys_exit(42);
    }

    struct linux_siginfo info;
    ltm_memset(&info, 0, sizeof(info));
    int ret = sys_waitid(1 /* P_PID */, (int)pid, &info, 4 /* WEXITED */, NULL);
    TEST_ASSERT(ret == 0, "waitid failed");
    TEST_ASSERT(info._sifields._sigchld._pid == (int)pid, "waitid pid mismatch");

    // Case B: Boundary / Edge Cases (WNOHANG when no child has exited)
    ret = sys_waitid(0 /* P_ALL */, 0, &info, 4 | 1 /* WEXITED | WNOHANG */, NULL);
    // Returns 0 or -ECHILD depending on whether other children exist
    TEST_ASSERT(ret == 0 || ret == -ECHILD, "waitid WNOHANG unexpected return");

    // Case C: Error / Errno Translation
    ret = sys_waitid(-1, (int)pid, &info, 4, NULL);
    TEST_ASSERT(ret == -EINVAL, "waitid(which=-1) must return -EINVAL");
    ret = sys_waitid(1, (int)pid, &info, 0, NULL);
    TEST_ASSERT(ret == -EINVAL, "waitid(options=0) must return -EINVAL");

    TEST_PASS("test_sys_095_waitid passed!");
    return 0;
}
