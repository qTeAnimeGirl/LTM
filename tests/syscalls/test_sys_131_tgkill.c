#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int my_pid = sys_getpid();
    int my_tid = sys_gettid();

    // Case A: Happy Path
    int ret = sys_tgkill(my_pid, my_tid, 0);
    TEST_ASSERT(ret == 0, "tgkill failed");

    // Case B: Boundary / Edge Cases
    ret = sys_tgkill(my_pid, my_tid, 0);
    TEST_ASSERT(ret == 0, "tgkill repeat failed");

    // Case C: Error / Errno Handling
    ret = sys_tgkill(my_pid, my_tid, 999);
    TEST_ASSERT(ret == -EINVAL, "tgkill(invalid_sig) must return -EINVAL");
    ret = sys_tgkill(-99999, my_tid, 0);
    TEST_ASSERT(ret == -ESRCH, "tgkill(bad_pid) must return -ESRCH");

    TEST_PASS("test_sys_131_tgkill passed!");
    return 0;
}
