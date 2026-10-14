#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int ret = sys_sethostname("test-hostname", 13);
    TEST_ASSERT(ret == 0, "sethostname failed");
    struct linux_utsname uts;
    ret = sys_uname(&uts);
    TEST_ASSERT(ret == 0, "uname failed");
    TEST_ASSERT(ltm_strcmp(uts.nodename, "test-hostname") == 0, "nodename did not update");

    // Case B: Boundary / Edge Cases (restore default)
    ret = sys_sethostname("ltm-mac", 7);
    TEST_ASSERT(ret == 0, "restore hostname failed");

    // Case C: Error / Errno Translation
    ret = sys_sethostname(NULL, 10);
    TEST_ASSERT(ret == -EFAULT, "sethostname(NULL) must return -EFAULT");
    ret = sys_sethostname("toolongname", 100);
    TEST_ASSERT(ret == -EINVAL, "sethostname(len > 64) must return -EINVAL");

    TEST_PASS("test_sys_161_sethostname passed!");
    return 0;
}
