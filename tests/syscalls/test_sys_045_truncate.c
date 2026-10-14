#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "truncate_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    char dummy[256];
    ltm_memset(dummy, 'A', sizeof(dummy));
    sys_write(fd, dummy, sizeof(dummy));
    sys_close(fd);

    // Case A: Happy Path
    int ret = sys_truncate(tmp, 100);
    TEST_ASSERT(ret == 0, "truncate(100) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_truncate(tmp, 0);
    TEST_ASSERT(ret == 0, "truncate(0) failed");

    // Case C: Error / Errno Handling
    ret = sys_truncate(NULL, 10);
    TEST_ASSERT(ret == -EFAULT, "truncate(NULL) must return -EFAULT");
    ret = sys_truncate("nonexistent_truncate_xyz", 10);
    TEST_ASSERT(ret == -ENOENT, "truncate(nonexistent) must return -ENOENT");
    ret = sys_truncate(tmp, -5);
    TEST_ASSERT(ret == -EINVAL, "truncate(negative) must return -EINVAL");

    TEST_PASS("test_sys_045_truncate passed!");
    return 0;
}
