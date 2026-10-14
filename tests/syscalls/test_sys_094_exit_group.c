#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (spawn child that terminates via exit_group)
    long pid = sys_clone(17 /* SIGCHLD */, NULL, NULL, NULL, NULL);
    TEST_ASSERT(pid >= 0, "clone failed");
    if (pid == 0) {
        sys_exit_group(42);
    }

    int status = 0;
    int ret = sys_wait4((int)pid, &status, 0, NULL);
    TEST_ASSERT(ret == (int)pid, "wait4 failed to reap child");
    int exit_code = (status >> 8) & 0xff;
    TEST_ASSERT(exit_code == 42, "child exit_group code mismatch");

    // Case B: Boundary / Edge Cases (wait4 with no children)
    ret = sys_wait4((int)pid, &status, 1 /* WNOHANG */, NULL);
    TEST_ASSERT(ret == -ECHILD, "wait4 on reaped child must return -ECHILD");

    // Case C: Error / Errno Handling (invalid pid)
    ret = sys_wait4(-99999, &status, 0, NULL);
    TEST_ASSERT(ret == -ECHILD, "wait4(-99999) must return -ECHILD");

    TEST_PASS("test_sys_094_exit_group passed!");
    return 0;
}
