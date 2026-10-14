#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int timerid = -1;
    int ret = sys_timer_create(0, NULL, &timerid);
    TEST_ASSERT(ret == 0, "timer_create failed");

    // Case A: Happy Path (Delete active timer)
    ret = sys_timer_delete(timerid);
    TEST_ASSERT(ret == 0, "timer_delete failed");

    // Case B: Boundary / Edge Cases (Delete already deleted timer)
    ret = sys_timer_delete(timerid);
    TEST_ASSERT(ret == -EINVAL, "timer_delete on inactive timer must return -EINVAL");

    // Case C: Error / Errno Translation
    ret = sys_timer_delete(-1);
    TEST_ASSERT(ret == -EINVAL, "timer_delete(bad id) must return -EINVAL");

    TEST_PASS("test_sys_111_timer_delete passed!");
    return 0;
}
