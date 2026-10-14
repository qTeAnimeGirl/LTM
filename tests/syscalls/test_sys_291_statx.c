#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    const char *tmp_file = "tmp_sys_291_statx.tmp";
    int fd = sys_openat(AT_FDCWD, tmp_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test_statx_data", 15);
    sys_close(fd);

    // Case A: Happy Path (Query file attributes via statx)
    struct statx stx;
    int ret = sys_statx(AT_FDCWD, tmp_file, 0, STATX_BASIC_STATS, &stx);
    TEST_ASSERT(ret == 0, "statx failed");
    TEST_ASSERT(stx.stx_size == 15, "statx size mismatch");
    TEST_ASSERT(stx.stx_uid == 0, "statx uid mismatch");

    // Case B: Boundary / Edge Cases (Query with AT_EMPTY_PATH on open fd)
    fd = sys_openat(AT_FDCWD, tmp_file, O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "openat failed");
    ret = sys_statx(fd, "", 0x1000 /* AT_EMPTY_PATH */, STATX_BASIC_STATS, &stx);
    TEST_ASSERT(ret == 0, "statx(AT_EMPTY_PATH) failed");
    TEST_ASSERT(stx.stx_size == 15, "statx(AT_EMPTY_PATH) size mismatch");
    sys_close(fd);

    // Case C: Error / Errno Translation
    ret = sys_statx(AT_FDCWD, NULL, 0, STATX_BASIC_STATS, &stx);
    TEST_ASSERT(ret == -EFAULT, "statx(NULL pathname) must return -EFAULT");

    ret = sys_statx(AT_FDCWD, tmp_file, 0, STATX_BASIC_STATS, NULL);
    TEST_ASSERT(ret == -EFAULT, "statx(NULL statxbuf) must return -EFAULT");

    ret = sys_statx(AT_FDCWD, "nonexistent_291.tmp", 0, STATX_BASIC_STATS, &stx);
    TEST_ASSERT(ret == -ENOENT, "statx(missing file) must return -ENOENT");

    ret = sys_statx(AT_FDCWD, tmp_file, 0x12340000, STATX_BASIC_STATS, &stx);
    TEST_ASSERT(ret == -EINVAL, "statx(invalid flags) must return -EINVAL");

    sys_unlinkat(AT_FDCWD, tmp_file, 0);

    TEST_PASS("test_sys_291_statx passed!");
    return 0;
}
