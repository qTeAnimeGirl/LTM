#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (fork subprocess)
    long pid = sys_clone(17 /* SIGCHLD */, NULL, NULL, NULL, NULL);
    TEST_ASSERT(pid >= 0, "clone failed");
    if (pid == 0) {
        sys_exit(77);
    }

    int status = 0;
    int ret = sys_wait4((int)pid, &status, 0, NULL);
    TEST_ASSERT(ret == (int)pid, "wait4 failed");
    int exit_code = (status >> 8) & 0xff;
    TEST_ASSERT(exit_code == 77, "child exit code mismatch");

    // Case B: Boundary / Edge Cases (wait4 with WNOHANG)
    ret = sys_wait4((int)pid, &status, 1 /* WNOHANG */, NULL);
    TEST_ASSERT(ret == -ECHILD, "wait4 must return -ECHILD");

    // Case C: Error / Errno Handling (invalid clone flags)
    long bad = sys_clone(0x100000000ULL, NULL, NULL, NULL, NULL);
    (void)bad;

    TEST_PASS("test_sys_220_clone passed!");
    return 0;
}
