#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Create temporary file
    int fd = sys_openat(AT_FDCWD, "/tmp/ltm_test_fchmodat2.tmp", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat create tmp file failed");
    sys_close(fd);

    // Case A: Happy Path (Change mode to 0600)
    int ret = sys_fchmodat2(AT_FDCWD, "/tmp/ltm_test_fchmodat2.tmp", 0600, 0);
    TEST_ASSERT(ret == 0, "fchmodat2 failed");

    // Case B: Boundary / Edge Cases (Change back to 0644)
    ret = sys_fchmodat2(AT_FDCWD, "/tmp/ltm_test_fchmodat2.tmp", 0644, 0);
    TEST_ASSERT(ret == 0, "fchmodat2 back to 0644 failed");

    // Case C: Error / Errno Handling (Nonexistent path -> -ENOENT, NULL path -> -EFAULT)
    ret = sys_fchmodat2(AT_FDCWD, "/tmp/ltm_nonexistent_fchmodat2.tmp", 0644, 0);
    TEST_ASSERT(ret == -ENOENT, "fchmodat2 nonexistent must return -ENOENT");

    ret = sys_fchmodat2(AT_FDCWD, NULL, 0644, 0);
    TEST_ASSERT(ret == -EFAULT, "fchmodat2(NULL) must return -EFAULT");

    sys_unlinkat(AT_FDCWD, "/tmp/ltm_test_fchmodat2.tmp", 0);
    TEST_PASS("test_sys_452_fchmodat2 passed!");
    return 0;
}
