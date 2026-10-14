#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char handle_buf[64];
    struct file_handle *fh = (struct file_handle *)handle_buf;
    fh->handle_bytes = 32;
    int mount_id = 0;

    // Case A: Happy Path (Standard call returns -EOPNOTSUPP on Darwin/APFS)
    int ret = sys_name_to_handle_at(AT_FDCWD, ".", fh, &mount_id, 0);
    TEST_ASSERT(ret == -EOPNOTSUPP, "name_to_handle_at must return -EOPNOTSUPP");

    // Case B: Boundary / Edge Cases (Query with AT_SYMLINK_FOLLOW = 0x400)
    ret = sys_name_to_handle_at(AT_FDCWD, ".", fh, &mount_id, 0x400);
    TEST_ASSERT(ret == -EOPNOTSUPP, "name_to_handle_at with flags must return -EOPNOTSUPP");

    // Case C: Error / Errno Handling (NULL pathname -> -EFAULT, NULL handle -> -EFAULT)
    ret = sys_name_to_handle_at(AT_FDCWD, NULL, fh, &mount_id, 0);
    TEST_ASSERT(ret == -EFAULT, "name_to_handle_at(NULL path) must return -EFAULT");

    ret = sys_name_to_handle_at(AT_FDCWD, ".", NULL, &mount_id, 0);
    TEST_ASSERT(ret == -EFAULT, "name_to_handle_at(NULL handle) must return -EFAULT");

    TEST_PASS("test_sys_264_name_to_handle_at passed!");
    return 0;
}
