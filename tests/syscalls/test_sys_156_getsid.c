#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int sid = sys_getsid(0);
    TEST_ASSERT(sid > 0, "getsid(0) > 0");

    // Case B: Boundary / Edge Cases
    int my_pid = sys_getpid();
    int sid2 = sys_getsid(my_pid);
    TEST_ASSERT(sid == sid2, "getsid(my_pid) == getsid(0)");

    // Case C: Error / Errno Handling
    int ret = sys_getsid(-1);
    TEST_ASSERT(ret == -EINVAL || ret == -ESRCH, "getsid(-1) must return -EINVAL or -ESRCH");

    TEST_PASS("test_sys_156_getsid passed!");
    return 0;
}
