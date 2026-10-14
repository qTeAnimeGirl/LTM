#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setpgid(0, 0);
    TEST_ASSERT(ret >= 0 || ret == -EACCES || ret == -EPERM, "setpgid(0, 0) unexpected error");

    // Case B: Boundary / Edge Cases
    int my_pid = sys_getpid();
    int pgid = sys_getpgid(0);
    ret = sys_setpgid(my_pid, pgid);
    TEST_ASSERT(ret >= 0 || ret == -EACCES || ret == -EPERM, "setpgid(my_pid, pgid) failed");

    // Case C: Error / Errno Handling
    ret = sys_setpgid(-999, 0);
    TEST_ASSERT(ret == -EINVAL || ret == -ESRCH, "setpgid(-999) must return -EINVAL or -ESRCH");

    TEST_PASS("test_sys_154_setpgid passed!");
    return 0;
}
