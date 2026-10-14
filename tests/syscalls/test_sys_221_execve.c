#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (execute test_hello in child)
    long pid = sys_clone(17 /* SIGCHLD */, NULL, NULL, NULL, NULL);
    TEST_ASSERT(pid >= 0, "clone failed");
    if (pid == 0) {
        char *args[] = {"/tests/bin/test_hello", NULL};
        char *env[] = {NULL};
        sys_execve("/tests/bin/test_hello", args, env);
        sys_exit(1);
    }

    int status = 0;
    int ret = sys_wait4((int)pid, &status, 0, NULL);
    TEST_ASSERT(ret == (int)pid, "wait4 on execve child failed");

    // Case B: Boundary / Edge Cases (execve nonexistent binary)
    pid = sys_clone(17, NULL, NULL, NULL, NULL);
    if (pid == 0) {
        char *args[] = {"/nonexistent_prog_xyz", NULL};
        char *env[] = {NULL};
        int err = sys_execve("/nonexistent_prog_xyz", args, env);
        sys_exit(err == -ENOENT ? 88 : 1);
    }
    sys_wait4((int)pid, &status, 0, NULL);
    TEST_ASSERT(((status >> 8) & 0xff) == 88, "execve nonexistent didn't return -ENOENT");

    // Case C: Error / Errno Handling (NULL filename)
    ret = sys_execve(NULL, NULL, NULL);
    TEST_ASSERT(ret == -EFAULT, "execve(NULL) must return -EFAULT");

    TEST_PASS("test_sys_221_execve passed!");
    return 0;
}
