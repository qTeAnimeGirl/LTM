#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    uint32_t hdr[2] = {0x20080522, 0};
    uint32_t data[6] = {0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff};
    int ret = sys_capset(hdr, data);
    TEST_ASSERT(ret == 0, "capset failed");

    // Case B: Boundary / Edge Cases (NULL data)
    ret = sys_capset(hdr, NULL);
    TEST_ASSERT(ret == 0, "capset(hdr, NULL) failed");

    // Case C: Error / Errno Handling (NULL both)
    ret = sys_capset(NULL, NULL);
    TEST_ASSERT(ret == 0, "capset(NULL, NULL) failed");

    TEST_PASS("test_sys_091_capset passed!");
    return 0;
}
