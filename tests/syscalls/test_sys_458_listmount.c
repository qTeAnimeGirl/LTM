#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char req[16];
    uint64_t mnt_ids[8];
    ltm_memset(req, 0, sizeof(req));

    // Case A: Happy Path (listmount returns -ENOENT for nonexistent mount ID)
    int ret = sys_listmount(req, mnt_ids, 8, 0);
    TEST_ASSERT(ret == -ENOENT, "listmount must return -ENOENT for missing mount");

    // Case B: Boundary / Edge Cases (Alternative flags)
    ret = sys_listmount(req, mnt_ids, 8, 1);
    TEST_ASSERT(ret == -ENOENT, "listmount(flags=1) must return -ENOENT");

    // Case C: Error / Errno Handling (NULL req -> -EFAULT, NULL ids -> -EFAULT, nr=0 -> -EINVAL)
    ret = sys_listmount(NULL, mnt_ids, 8, 0);
    TEST_ASSERT(ret == -EFAULT, "listmount(NULL req) must return -EFAULT");

    ret = sys_listmount(req, NULL, 8, 0);
    TEST_ASSERT(ret == -EFAULT, "listmount(NULL ids) must return -EFAULT");

    ret = sys_listmount(req, mnt_ids, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "listmount(nr=0) must return -EINVAL");

    TEST_PASS("test_sys_458_listmount passed!");
    return 0;
}
