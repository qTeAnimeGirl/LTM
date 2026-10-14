#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setgid(0);
    TEST_ASSERT(ret == 0, "setgid(0) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_setgid(sys_getgid());
    TEST_ASSERT(ret == 0, "setgid(current) failed");

    // Case C: Error / Errno Handling (invalid gid)
    ret = sys_setgid(-9999);
    TEST_ASSERT(ret == -EINVAL, "setgid(-9999) must return -EINVAL");

    TEST_PASS("test_sys_144_setgid passed!");
    return 0;
}
