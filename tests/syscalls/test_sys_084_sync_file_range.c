#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *path = "tmp_sfr_084.txt";
    int fd = sys_openat(AT_FDCWD, path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "test_data_for_sync_range", 24);

    // Case A: Happy Path
    int ret = sys_sync_file_range(fd, 0, 24, 0);
    TEST_ASSERT(ret == 0, "sync_file_range happy path failed");

    // Case B: Boundary / Edge Cases (nbytes=0 syncs through EOF)
    ret = sys_sync_file_range(fd, 0, 0, 0);
    TEST_ASSERT(ret == 0, "sync_file_range(nbytes=0) failed");

    // Case C: Error / Errno Translation
    ret = sys_sync_file_range(-1, 0, 24, 0);
    TEST_ASSERT(ret == -EBADF, "sync_file_range(fd=-1) must return -EBADF");
    ret = sys_sync_file_range(fd, -1, 24, 0);
    TEST_ASSERT(ret == -EINVAL, "sync_file_range(offset < 0) must return -EINVAL");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, path, 0);

    TEST_PASS("test_sys_084_sync_file_range passed!");
    return 0;
}
