#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char handle_buf[64];
    struct file_handle *fh = (struct file_handle *)handle_buf;
    fh->handle_bytes = 32;

    // Case A: Happy Path (Standard call returns -EOPNOTSUPP)
    int ret = sys_open_by_handle_at(AT_FDCWD, fh, O_RDONLY);
    TEST_ASSERT(ret == -EOPNOTSUPP, "open_by_handle_at must return -EOPNOTSUPP");

    // Case B: Boundary / Edge Cases (open_by_handle_at with O_RDWR)
    ret = sys_open_by_handle_at(AT_FDCWD, fh, O_RDWR);
    TEST_ASSERT(ret == -EOPNOTSUPP, "open_by_handle_at(RDWR) must return -EOPNOTSUPP");

    // Case C: Error / Errno Handling (Bad mount fd -> -EBADF, NULL handle -> -EFAULT)
    ret = sys_open_by_handle_at(-1, fh, O_RDONLY);
    TEST_ASSERT(ret == -EBADF, "open_by_handle_at(-1) must return -EBADF");

    ret = sys_open_by_handle_at(AT_FDCWD, NULL, O_RDONLY);
    TEST_ASSERT(ret == -EFAULT, "open_by_handle_at(NULL) must return -EFAULT");

    TEST_PASS("test_sys_265_open_by_handle_at passed!");
    return 0;
}
