#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "ftruncate_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    char dummy[256];
    ltm_memset(dummy, 'B', sizeof(dummy));
    sys_write(fd, dummy, sizeof(dummy));

    // Case A: Happy Path
    int ret = sys_ftruncate(fd, 50);
    TEST_ASSERT(ret == 0, "ftruncate(50) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_ftruncate(fd, 0);
    TEST_ASSERT(ret == 0, "ftruncate(0) failed");

    // Case C: Error / Errno Handling
    ret = sys_ftruncate(-1, 10);
    TEST_ASSERT(ret == -EBADF, "ftruncate(-1) must return -EBADF");
    ret = sys_ftruncate(fd, -5);
    TEST_ASSERT(ret == -EINVAL, "ftruncate(negative) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_046_ftruncate passed!");
    return 0;
}
