#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *fname = "tmp_unlinkat_035.tmp";

    int fd = sys_openat(AT_FDCWD, fname, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_unlinkat(AT_FDCWD, fname, 0);
    TEST_ASSERT(ret == 0, "unlinkat failed");

    // Case B: Boundary / Edge Cases (already removed)
    ret = sys_unlinkat(AT_FDCWD, fname, 0);
    TEST_ASSERT(ret == -ENOENT, "unlinkat(nonexistent) must return -ENOENT");

    // Case C: Error / Errno Handling
    ret = sys_unlinkat(AT_FDCWD, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "unlinkat(NULL) must return -EFAULT");
    ret = sys_unlinkat(-999, fname, 0);
    TEST_ASSERT(ret == -EBADF, "unlinkat(invalid_dirfd) must return -EBADF");

    TEST_PASS("test_sys_035_unlinkat passed!");
    return 0;
}
