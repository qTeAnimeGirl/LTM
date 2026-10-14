#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (PTRACE_TRACEME = 0)
    int ret = sys_ptrace(0 /* PTRACE_TRACEME */, 0, NULL, NULL);
    TEST_ASSERT(ret == 0, "ptrace(PTRACE_TRACEME) failed");

    // Case B: Boundary / Edge Cases (PTRACE_ATTACH to non-existent pid -> -ESRCH or -EPERM)
    ret = sys_ptrace(16 /* PTRACE_ATTACH */, 99999, NULL, NULL);
    TEST_ASSERT(ret == -ESRCH || ret == -EPERM, "ptrace(ATTACH) returned unexpected code");

    // Case C: Error / Errno Handling (Negative pid -> -ESRCH, invalid request -> -EIO)
    ret = sys_ptrace(16 /* PTRACE_ATTACH */, -1, NULL, NULL);
    TEST_ASSERT(ret == -ESRCH, "ptrace(-1 pid) must return -ESRCH");

    ret = sys_ptrace(999999 /* invalid req */, 0, NULL, NULL);
    TEST_ASSERT(ret == -EIO, "ptrace(invalid req) must return -EIO");

    TEST_PASS("test_sys_117_ptrace passed!");
    return 0;
}
