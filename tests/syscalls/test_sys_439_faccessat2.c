#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "faccessat2_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_faccessat2(AT_FDCWD, tmp, R_OK | W_OK, 0);
    TEST_ASSERT(ret == 0, "faccessat2 failed");

    // Case B: Boundary / Edge Cases
    ret = sys_faccessat2(AT_FDCWD, tmp, F_OK, AT_EACCESS);
    TEST_ASSERT(ret == 0, "faccessat2(AT_EACCESS) failed");

    // Case C: Error / Errno Handling
    ret = sys_faccessat2(AT_FDCWD, NULL, F_OK, 0);
    TEST_ASSERT(ret == -EFAULT, "faccessat2(NULL) must return -EFAULT");
    ret = sys_faccessat2(AT_FDCWD, "nonexistent_faccessat2", F_OK, 0);
    TEST_ASSERT(ret == -ENOENT, "faccessat2(nonexistent) must return -ENOENT");
    ret = sys_faccessat2(-999, tmp, F_OK, 0);
    TEST_ASSERT(ret == -EBADF, "faccessat2(bad_dirfd) must return -EBADF");
    ret = sys_faccessat2(AT_FDCWD, tmp, F_OK, 0xffffff);
    TEST_ASSERT(ret == -EINVAL, "faccessat2(invalid_flags) must return -EINVAL");

    TEST_PASS("test_sys_439_faccessat2 passed!");
    return 0;
}
