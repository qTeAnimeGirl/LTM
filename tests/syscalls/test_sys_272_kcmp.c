#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    pid_t pid = sys_getpid();

    // Case A: Happy Path (Compare self process VM: KCMP_VM = 1 -> returns 0)
    int ret = sys_kcmp(pid, pid, 1 /* KCMP_VM */, 0, 0);
    TEST_ASSERT(ret == 0, "kcmp(self, KCMP_VM) failed");

    // Case B: Boundary / Edge Cases (Compare self file descriptors: KCMP_FILE = 0)
    ret = sys_kcmp(pid, pid, 0 /* KCMP_FILE */, 0, 0);
    TEST_ASSERT(ret == 0, "kcmp(self, KCMP_FILE, 0, 0) failed");

    // Case C: Error / Errno Handling (Invalid type -> -EINVAL, negative pid -> -ESRCH)
    ret = sys_kcmp(pid, pid, 999 /* invalid type */, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "kcmp(invalid type) must return -EINVAL");

    ret = sys_kcmp(-1, pid, 1, 0, 0);
    TEST_ASSERT(ret == -ESRCH, "kcmp(-1 pid) must return -ESRCH");

    TEST_PASS("test_sys_272_kcmp passed!");
    return 0;
}
