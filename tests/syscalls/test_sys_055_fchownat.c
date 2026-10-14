#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fchownat_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    // Case A: Happy Path (-1, -1 no-op)
    int ret = sys_fchownat(AT_FDCWD, tmp, -1, -1, 0);
    TEST_ASSERT(ret == 0, "fchownat(-1, -1) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fchownat(AT_FDCWD, tmp, 0, 0, 0);
    TEST_ASSERT(ret == 0, "fchownat(0, 0) failed");

    // Case C: Error / Errno Handling
    ret = sys_fchownat(AT_FDCWD, NULL, 0, 0, 0);
    TEST_ASSERT(ret == -EFAULT, "fchownat(NULL) must return -EFAULT");
    ret = sys_fchownat(AT_FDCWD, "nonexistent_fchownat", 0, 0, 0);
    TEST_ASSERT(ret == -ENOENT, "fchownat(nonexistent) must return -ENOENT");
    ret = sys_fchownat(-999, tmp, 0, 0, 0);
    TEST_ASSERT(ret == -EBADF, "fchownat(invalid_dirfd) must return -EBADF");

    TEST_PASS("test_sys_055_fchownat passed!");
    return 0;
}
