#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fstat_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "12345", 5);

    // Case A: Happy Path
    struct linux_stat st;
    int ret = sys_fstat(fd, &st);
    TEST_ASSERT(ret == 0, "fstat failed");
    TEST_ASSERT(st.st_size == 5, "fstat reported wrong size");

    // Case B: Boundary / Edge Cases (fstat on stdout)
    ret = sys_fstat(1, &st);
    TEST_ASSERT(ret == 0, "fstat(stdout) failed");

    // Case C: Error / Errno Handling
    ret = sys_fstat(-1, &st);
    TEST_ASSERT(ret == -EBADF, "fstat(-1) must return -EBADF");
    ret = sys_fstat(fd, NULL);
    TEST_ASSERT(ret == -EFAULT, "fstat(NULL) must return -EFAULT");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, tmp, 0);

    TEST_PASS("test_sys_080_fstat passed!");
    return 0;
}
