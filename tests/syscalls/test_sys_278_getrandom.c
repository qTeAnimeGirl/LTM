#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    char buf[64];
    ltm_memset(buf, 0, sizeof(buf));

    // Case A: Happy Path
    ssize_t ret = sys_getrandom(buf, 32, 0);
    TEST_ASSERT(ret == 32, "getrandom failed to read 32 bytes");
    bool has_nonzero = false;
    for (int i = 0; i < 32; i++) {
        if (buf[i] != 0) has_nonzero = true;
    }
    TEST_ASSERT(has_nonzero, "getrandom returned all zeros");

    // Case B: Boundary / Edge Cases
    ret = sys_getrandom(buf, 0, 0);
    TEST_ASSERT(ret == 0, "getrandom(buflen=0) must return 0");

    // Case C: Error / Errno Handling
    ret = sys_getrandom(NULL, 32, 0);
    TEST_ASSERT(ret == -EFAULT, "getrandom(NULL) must return -EFAULT");
    ret = sys_getrandom(buf, 32, 0xffffff);
    TEST_ASSERT(ret == -EINVAL, "getrandom(invalid_flags) must return -EINVAL");

    TEST_PASS("test_sys_278_getrandom passed!");
    return 0;
}
