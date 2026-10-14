#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Correct magics with unprivileged caller -> EPERM)
    int ret = sys_reboot(0xfee1dead, 672274793, 0 /* LINUX_REBOOT_CMD_RESTART */, NULL);
    TEST_ASSERT(ret == -EPERM, "reboot with valid magics must return -EPERM");

    // Case B: Boundary / Edge Cases (Alternative valid magic2 constants: 85072278, 369367448)
    ret = sys_reboot(0xfee1dead, 85072278, 0, NULL);
    TEST_ASSERT(ret == -EPERM, "reboot with magic2 85072278 must return -EPERM");

    // Case C: Error / Errno Handling (Invalid magic numbers -> -EINVAL)
    ret = sys_reboot(0x12345678, 672274793, 0, NULL);
    TEST_ASSERT(ret == -EINVAL, "reboot with invalid magic1 must return -EINVAL");

    ret = sys_reboot(0xfee1dead, 0x12345678, 0, NULL);
    TEST_ASSERT(ret == -EINVAL, "reboot with invalid magic2 must return -EINVAL");

    TEST_PASS("test_sys_142_reboot passed!");
    return 0;
}
