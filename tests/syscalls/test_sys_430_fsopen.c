#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Valid filesystem type returns -EPERM in unprivileged sandbox)
    int ret = sys_fsopen("ext4", 0);
    TEST_ASSERT(ret == -EPERM, "fsopen must return -EPERM in sandbox");

    // Case B: Boundary / Edge Cases (tmpfs type)
    ret = sys_fsopen("tmpfs", 0);
    TEST_ASSERT(ret == -EPERM, "fsopen(tmpfs) must return -EPERM");

    // Case C: Error / Errno Handling (NULL fsname -> -EFAULT)
    ret = sys_fsopen(NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "fsopen(NULL) must return -EFAULT");

    TEST_PASS("test_sys_430_fsopen passed!");
    return 0;
}
