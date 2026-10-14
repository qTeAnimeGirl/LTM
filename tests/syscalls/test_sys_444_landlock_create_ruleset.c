#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    struct landlock_ruleset_attr attr;
    attr.handled_access_fs = 1;
    attr.handled_access_net = 0;

    // Case A: Happy Path (Valid ruleset creation returns -ENOSYS on non-Linux kernel)
    int ret = sys_landlock_create_ruleset(&attr, sizeof(attr), 0);
    TEST_ASSERT(ret == -ENOSYS, "landlock_create_ruleset must return -ENOSYS");

    // Case B: Boundary / Edge Cases (Alternative flags)
    ret = sys_landlock_create_ruleset(&attr, sizeof(attr), 0);
    TEST_ASSERT(ret == -ENOSYS, "landlock_create_ruleset repeated call failed");

    // Case C: Error / Errno Handling (NULL attr -> -EFAULT, size < 8 -> -EINVAL, bad flags -> -EINVAL)
    ret = sys_landlock_create_ruleset(NULL, sizeof(attr), 0);
    TEST_ASSERT(ret == -EFAULT, "landlock_create_ruleset(NULL) must return -EFAULT");

    ret = sys_landlock_create_ruleset(&attr, 4, 0);
    TEST_ASSERT(ret == -EINVAL, "landlock_create_ruleset(size < 8) must return -EINVAL");

    ret = sys_landlock_create_ruleset(&attr, sizeof(attr), 999);
    TEST_ASSERT(ret == -EINVAL, "landlock_create_ruleset(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_444_landlock_create_ruleset passed!");
    return 0;
}
