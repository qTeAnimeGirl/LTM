#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fchmodat_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_fchmodat(AT_FDCWD, tmp, 0644, 0);
    TEST_ASSERT(ret == 0, "fchmodat failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fchmodat(AT_FDCWD, tmp, 0777, 0);
    TEST_ASSERT(ret == 0, "fchmodat(0777) failed");

    // Case C: Error / Errno Handling
    ret = sys_fchmodat(AT_FDCWD, NULL, 0644, 0);
    TEST_ASSERT(ret == -EFAULT, "fchmodat(NULL) must return -EFAULT");
    ret = sys_fchmodat(AT_FDCWD, "nonexistent_fchmodat", 0644, 0);
    TEST_ASSERT(ret == -ENOENT, "fchmodat(nonexistent) must return -ENOENT");
    ret = sys_fchmodat(-999, tmp, 0644, 0);
    TEST_ASSERT(ret == -EBADF, "fchmodat(invalid_dirfd) must return -EBADF");

    TEST_PASS("test_sys_053_fchmodat passed!");
    return 0;
}
