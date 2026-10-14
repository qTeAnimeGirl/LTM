#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_vhangup();
    TEST_ASSERT(ret == 0, "vhangup failed");

    // Case B: Boundary / Edge Cases (repeated call)
    ret = sys_vhangup();
    TEST_ASSERT(ret == 0, "vhangup repeated call failed");

    // Case C: Non-destructive verification (standard stdout still writeable)
    ssize_t nw = sys_write(1, "", 0);
    TEST_ASSERT(nw == 0, "stdout broken after vhangup");

    TEST_PASS("test_sys_058_vhangup passed!");
    return 0;
}
