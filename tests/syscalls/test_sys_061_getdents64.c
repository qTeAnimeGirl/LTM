#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int fd = sys_openat(AT_FDCWD, "/proc", O_RDONLY | O_DIRECTORY, 0);
    TEST_ASSERT(fd >= 0, "openat('/proc') failed");

    // Case A: Happy Path
    char buf[2048];
    int ret = sys_getdents64(fd, buf, sizeof(buf));
    TEST_ASSERT(ret > 0, "getdents64 failed to read /proc");

    // Case B: Boundary / Edge Cases (getdents64 with small buffer)
    char small_buf[512];
    ret = sys_getdents64(fd, small_buf, sizeof(small_buf));
    TEST_ASSERT(ret >= 0, "getdents64 with small buffer failed");

    // Case C: Error / Errno Handling
    ret = sys_getdents64(-1, buf, sizeof(buf));
    TEST_ASSERT(ret == -EBADF, "getdents64(-1) must return -EBADF");
    ret = sys_getdents64(fd, NULL, sizeof(buf));
    TEST_ASSERT(ret == -EFAULT, "getdents64(NULL) must return -EFAULT");

    sys_close(fd);

    TEST_PASS("test_sys_061_getdents64 passed!");
    return 0;
}
