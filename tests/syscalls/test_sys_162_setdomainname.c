#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_setdomainname("testdomain", 10);
    TEST_ASSERT(ret == 0, "setdomainname failed");

    // Case B: Boundary / Edge Cases (restore default)
    ret = sys_setdomainname("(none)", 6);
    TEST_ASSERT(ret == 0, "restore domainname failed");

    // Case C: Error / Errno Translation
    ret = sys_setdomainname(NULL, 10);
    TEST_ASSERT(ret == -EFAULT, "setdomainname(NULL) must return -EFAULT");
    ret = sys_setdomainname("toolongdomain", 100);
    TEST_ASSERT(ret == -EINVAL, "setdomainname(len > 64) must return -EINVAL");

    TEST_PASS("test_sys_162_setdomainname passed!");
    return 0;
}
