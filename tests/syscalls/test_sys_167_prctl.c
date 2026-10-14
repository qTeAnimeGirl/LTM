#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (PR_SET_NAME / PR_GET_NAME)
    int ret = sys_prctl(PR_SET_NAME, (long)"ltm_worker", 0, 0, 0);
    TEST_ASSERT(ret == 0, "prctl(PR_SET_NAME) failed");
    char name[32];
    ltm_memset(name, 0, sizeof(name));
    ret = sys_prctl(PR_GET_NAME, (long)name, 0, 0, 0);
    TEST_ASSERT(ret == 0, "prctl(PR_GET_NAME) failed");

    // Case B: Boundary / Edge Cases (PR_GET_DUMPABLE, PR_CAPBSET_READ)
    ret = sys_prctl(PR_GET_DUMPABLE, 0, 0, 0, 0);
    TEST_ASSERT(ret == 1, "prctl(PR_GET_DUMPABLE) must return 1");
    ret = sys_prctl(PR_CAPBSET_READ, 0, 0, 0, 0);
    TEST_ASSERT(ret == 1, "prctl(PR_CAPBSET_READ) must return 1");

    // Case C: Error / Errno Translation
    ret = sys_prctl(PR_SET_NAME, 0, 0, 0, 0);
    TEST_ASSERT(ret == -EFAULT, "prctl(PR_SET_NAME, NULL) must return -EFAULT");
    ret = sys_prctl(99999, 0, 0, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "prctl(invalid option) must return -EINVAL");

    TEST_PASS("test_sys_167_prctl passed!");
    return 0;
}
