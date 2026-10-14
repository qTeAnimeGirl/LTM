#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    long pid = sys_clone(17 /* SIGCHLD */, NULL, NULL, NULL, NULL);
    TEST_ASSERT(pid >= 0, "clone failed");
    if (pid == 0) {
        sys_exit(55);
    }

    int status = 0;
    int ret = sys_wait4((int)pid, &status, 0, NULL);
    TEST_ASSERT(ret == (int)pid, "wait4 failed");
    TEST_ASSERT(((status >> 8) & 0xff) == 55, "exit code mismatch");

    // Case B: Boundary / Edge Cases (WNOHANG when no child ready)
    ret = sys_wait4(-1, &status, 1 /* WNOHANG */, NULL);
    TEST_ASSERT(ret == -ECHILD || ret == 0, "wait4(WNOHANG) with no children must return -ECHILD or 0");

    // Case C: Error / Errno Handling (invalid pid)
    ret = sys_wait4(-99999, &status, 0, NULL);
    TEST_ASSERT(ret == -ECHILD, "wait4(-99999) must return -ECHILD");

    TEST_PASS("test_sys_260_wait4 passed!");
    return 0;
}
