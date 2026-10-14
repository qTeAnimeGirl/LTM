#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *oldpath = "orig_037.tmp";
    const char *newpath = "hardlink_037.tmp";

    int fd = sys_openat(AT_FDCWD, oldpath, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);
    sys_unlinkat(AT_FDCWD, newpath, 0);

    // Case A: Happy Path
    int ret = sys_linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0);
    TEST_ASSERT(ret == 0, "linkat failed");

    // Case B: Boundary / Edge Cases (verify link existence)
    int fd2 = sys_openat(AT_FDCWD, newpath, O_RDONLY, 0);
    TEST_ASSERT(fd2 >= 0, "openat on hardlink failed");
    sys_close(fd2);

    // Case C: Error / Errno Handling
    ret = sys_linkat(AT_FDCWD, NULL, AT_FDCWD, "bad_link", 0);
    TEST_ASSERT(ret == -EFAULT, "linkat(old=NULL) must return -EFAULT");
    ret = sys_linkat(AT_FDCWD, oldpath, AT_FDCWD, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "linkat(new=NULL) must return -EFAULT");

    sys_unlinkat(AT_FDCWD, oldpath, 0);
    sys_unlinkat(AT_FDCWD, newpath, 0);

    TEST_PASS("test_sys_037_linkat passed!");
    return 0;
}
