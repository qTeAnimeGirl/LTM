#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fstatfs_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    struct linux_statfs sfs;
    ltm_memset(&sfs, 0, sizeof(sfs));

    // Case A: Happy Path
    int ret = sys_fstatfs(fd, &sfs);
    TEST_ASSERT(ret == 0, "fstatfs(fd) failed");
    TEST_ASSERT(sfs.f_bsize > 0, "fstatfs.f_bsize > 0");

    // Case B: Boundary / Edge Cases (stdin fd 0)
    ret = sys_fstatfs(0, &sfs);
    TEST_ASSERT(ret == 0 || ret == -EBADF, "fstatfs(0) unexpected return");

    // Case C: Error / Errno Handling
    ret = sys_fstatfs(-1, &sfs);
    TEST_ASSERT(ret == -EBADF, "fstatfs(-1) must return -EBADF");
    ret = sys_fstatfs(fd, NULL);
    TEST_ASSERT(ret == -EFAULT, "fstatfs(fd, NULL) must return -EFAULT");

    sys_close(fd);
    TEST_PASS("test_sys_044_fstatfs passed!");
    return 0;
}
