#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char req[16];
    char buf[64];
    ltm_memset(req, 0, sizeof(req));
    ltm_memset(buf, 0, sizeof(buf));

    // Case A: Happy Path (statmount returns -ENOENT for nonexistent mount ID)
    int ret = sys_statmount(req, buf, sizeof(buf), 0);
    TEST_ASSERT(ret == -ENOENT, "statmount must return -ENOENT for missing mount");

    // Case B: Boundary / Edge Cases (Alternative flags)
    ret = sys_statmount(req, buf, sizeof(buf), 1);
    TEST_ASSERT(ret == -ENOENT, "statmount(flags=1) must return -ENOENT");

    // Case C: Error / Errno Handling (NULL req -> -EFAULT, NULL buf -> -EFAULT, bufsize=0 -> -EINVAL)
    ret = sys_statmount(NULL, buf, sizeof(buf), 0);
    TEST_ASSERT(ret == -EFAULT, "statmount(NULL req) must return -EFAULT");

    ret = sys_statmount(req, NULL, sizeof(buf), 0);
    TEST_ASSERT(ret == -EFAULT, "statmount(NULL buf) must return -EFAULT");

    ret = sys_statmount(req, buf, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "statmount(bufsize=0) must return -EINVAL");

    TEST_PASS("test_sys_457_statmount passed!");
    return 0;
}
