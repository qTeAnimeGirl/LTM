#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int pgid = sys_getpgid(0);
    TEST_ASSERT(pgid > 0, "getpgid(0) > 0");

    // Case B: Boundary / Edge Cases
    int my_pid = sys_getpid();
    int pgid2 = sys_getpgid(my_pid);
    TEST_ASSERT(pgid == pgid2, "getpgid(my_pid) == getpgid(0)");

    // Case C: Error / Errno Handling
    int ret = sys_getpgid(-1);
    TEST_ASSERT(ret == -EINVAL || ret == -ESRCH, "getpgid(-1) must return -EINVAL or -ESRCH");

    TEST_PASS("test_sys_155_getpgid passed!");
    return 0;
}
