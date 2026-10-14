#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (unshare with 0 flags succeeds)
    int ret = sys_unshare(0);
    TEST_ASSERT(ret == 0, "unshare(0) failed");

    // Case B: Boundary / Edge Cases (unshare with CLONE_FS=0x200 or CLONE_FILES=0x400 -> EPERM in container)
    ret = sys_unshare(0x00020000 /* CLONE_NEWNS */);
    TEST_ASSERT(ret == -EPERM, "unshare(CLONE_NEWNS) must return -EPERM");

    // Case C: Error / Errno Handling (Invalid flags -> -EINVAL)
    ret = sys_unshare(0x00000001 /* invalid flag */);
    TEST_ASSERT(ret == -EINVAL, "unshare(invalid flags) must return -EINVAL");

    TEST_PASS("test_sys_097_unshare passed!");
    return 0;
}
