#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    uint32_t hdr[2] = {0, 0};
    uint32_t data[6] = {0};
    int ret = sys_capget(hdr, data);
    TEST_ASSERT(ret == 0, "capget failed");
    TEST_ASSERT(hdr[0] == 0x20080522, "capget version mismatch");
    TEST_ASSERT(data[0] == 0xffffffff, "capget effective caps mismatch");

    // Case B: Boundary / Edge Cases (NULL data)
    ret = sys_capget(hdr, NULL);
    TEST_ASSERT(ret == 0, "capget(hdr, NULL) failed");

    // Case C: Error / Errno Handling (NULL hdr and data is benign in LTM)
    ret = sys_capget(NULL, NULL);
    TEST_ASSERT(ret == 0, "capget(NULL, NULL) failed");

    TEST_PASS("test_sys_090_capget passed!");
    return 0;
}
