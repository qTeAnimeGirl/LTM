#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *src = "src_038.tmp";
    const char *dst = "dst_038.tmp";

    int fd = sys_openat(AT_FDCWD, src, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);
    sys_unlinkat(AT_FDCWD, dst, 0);

    // Case A: Happy Path
    int ret = sys_renameat(AT_FDCWD, src, AT_FDCWD, dst);
    TEST_ASSERT(ret == 0, "renameat failed");

    // Case B: Boundary / Edge Cases (src no longer exists, dst exists)
    int fd_src = sys_openat(AT_FDCWD, src, O_RDONLY, 0);
    TEST_ASSERT(fd_src == -ENOENT, "src file should no longer exist");
    int fd_dst = sys_openat(AT_FDCWD, dst, O_RDONLY, 0);
    TEST_ASSERT(fd_dst >= 0, "dst file should exist");
    sys_close(fd_dst);

    // Case C: Error / Errno Handling
    ret = sys_renameat(AT_FDCWD, NULL, AT_FDCWD, dst);
    TEST_ASSERT(ret == -EFAULT, "renameat(src=NULL) must return -EFAULT");
    ret = sys_renameat(AT_FDCWD, dst, AT_FDCWD, NULL);
    TEST_ASSERT(ret == -EFAULT, "renameat(dst=NULL) must return -EFAULT");

    sys_unlinkat(AT_FDCWD, dst, 0);

    TEST_PASS("test_sys_038_renameat passed!");
    return 0;
}
