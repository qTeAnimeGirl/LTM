#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char dummy_attr[64];
    ltm_memset(dummy_attr, 0, sizeof(dummy_attr));

    // Case A: Happy Path (mount_setattr returns -EPERM for sandbox security)
    int ret = sys_mount_setattr(AT_FDCWD, "/tmp", 0, dummy_attr, sizeof(dummy_attr));
    TEST_ASSERT(ret == -EPERM, "mount_setattr must return -EPERM in sandbox");

    // Case B: Boundary / Edge Cases (Alternative path)
    ret = sys_mount_setattr(AT_FDCWD, "/", 0, dummy_attr, sizeof(dummy_attr));
    TEST_ASSERT(ret == -EPERM, "mount_setattr(/) must return -EPERM");

    // Case C: Error / Errno Handling (NULL path -> -EFAULT, usize < 32 -> -EINVAL)
    ret = sys_mount_setattr(AT_FDCWD, NULL, 0, dummy_attr, sizeof(dummy_attr));
    TEST_ASSERT(ret == -EFAULT, "mount_setattr(NULL path) must return -EFAULT");

    ret = sys_mount_setattr(AT_FDCWD, "/tmp", 0, dummy_attr, 16);
    TEST_ASSERT(ret == -EINVAL, "mount_setattr(usize < 32) must return -EINVAL");

    TEST_PASS("test_sys_442_mount_setattr passed!");
    return 0;
}
