#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char buf[256];

    // Case A: Happy Path (Standard lookup of cookie returns -ENOSYS or -EINVAL)
    int ret = sys_lookup_dcookie(12345, buf, sizeof(buf));
    TEST_ASSERT(ret == -ENOSYS || ret == -EINVAL, "lookup_dcookie returned unexpected code");

    // Case B: Boundary / Edge Cases (len == 0 -> -EINVAL)
    ret = sys_lookup_dcookie(12345, buf, 0);
    TEST_ASSERT(ret == -EINVAL, "lookup_dcookie(len == 0) must return -EINVAL");

    // Case C: Error / Errno Handling (NULL buffer -> -EFAULT)
    ret = sys_lookup_dcookie(12345, NULL, sizeof(buf));
    TEST_ASSERT(ret == -EFAULT, "lookup_dcookie(NULL) must return -EFAULT");

    TEST_PASS("test_sys_018_lookup_dcookie passed!");
    return 0;
}
