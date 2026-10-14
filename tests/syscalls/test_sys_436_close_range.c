#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "close_range_test.tmp";
    int f1 = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    int f2 = sys_openat(AT_FDCWD, tmp, O_RDONLY, 0);
    int f3 = sys_openat(AT_FDCWD, tmp, O_RDONLY, 0);
    TEST_ASSERT(f1 >= 0 && f2 >= 0 && f3 >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_close_range((unsigned int)f1, (unsigned int)f3, 0);
    TEST_ASSERT(ret == 0, "close_range failed");
    TEST_ASSERT(sys_close(f1) == -EBADF, "f1 must be closed");
    TEST_ASSERT(sys_close(f2) == -EBADF, "f2 must be closed");
    TEST_ASSERT(sys_close(f3) == -EBADF, "f3 must be closed");

    // Case B: Boundary / Edge Cases
    int f4 = sys_openat(AT_FDCWD, tmp, O_RDONLY, 0);
    TEST_ASSERT(f4 >= 0, "openat failed");
    ret = sys_close_range((unsigned int)f4, (unsigned int)f4, 0);
    TEST_ASSERT(ret == 0, "close_range(single_fd) failed");
    TEST_ASSERT(sys_close(f4) == -EBADF, "f4 must be closed");

    // Case C: Error / Errno Handling
    ret = sys_close_range(100, 10, 0);
    TEST_ASSERT(ret == -EINVAL, "close_range(first > last) must return -EINVAL");
    ret = sys_close_range(10, 20, 0xffffff);
    TEST_ASSERT(ret == -EINVAL, "close_range(invalid_flags) must return -EINVAL");

    TEST_PASS("test_sys_436_close_range passed!");
    return 0;
}
