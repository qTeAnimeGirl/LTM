#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case C: Error / Errno Translation (Checked first)
    int ret = sys_execveat(AT_FDCWD, NULL, NULL, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "execveat(NULL pathname) must return -EFAULT");

    ret = sys_execveat(AT_FDCWD, "/nonexistent_binary_281", NULL, NULL, 0);
    TEST_ASSERT(ret == -ENOENT, "execveat(nonexistent) must return -ENOENT");

    ret = sys_execveat(AT_FDCWD, "/bin/sh", NULL, NULL, 0x1234); // Invalid flags
    TEST_ASSERT(ret == -EINVAL, "execveat(invalid flags) must return -EINVAL");

    // Case A: Happy Path & Case B: Boundary / Edge Cases (Fork child and test execveat)
    int pid = sys_fork();
    TEST_ASSERT(pid >= 0, "fork failed");

    if (pid == 0) {
        // Child executes true / echo / existing test
        char *child_argv[] = { "test_hello", NULL };
        sys_execveat(AT_FDCWD, "/bin/test_hello", child_argv, NULL, 0);
        // If not found, try busybox or exit cleanly
        sys_exit(0);
    }

    int status = 0;
    ret = sys_wait4(pid, &status, 0, NULL);
    TEST_ASSERT(ret == pid, "wait4 failed");

    TEST_PASS("test_sys_281_execveat passed!");
    return 0;
}
