#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "read_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    const char *msg = "HELLO_READ";
    sys_write(fd, msg, 10);
    sys_lseek(fd, 0, SEEK_SET);

    // Case A: Happy Path
    char buf[32];
    ltm_memset(buf, 0, sizeof(buf));
    ssize_t r = sys_read(fd, buf, 10);
    TEST_ASSERT(r == 10, "read failed");
    TEST_ASSERT(ltm_memcmp(buf, msg, 10) == 0, "read data corrupted");

    // Case B: Boundary / Edge Cases
    r = sys_read(fd, buf, 0);
    TEST_ASSERT(r == 0, "read(count=0) must return 0");

    // Case C: Error / Errno Handling
    r = sys_read(-1, buf, 10);
    TEST_ASSERT(r == -EBADF, "read(-1) must return -EBADF");
    r = sys_read(fd, NULL, 10);
    TEST_ASSERT(r == -EFAULT, "read(NULL) must return -EFAULT");

    sys_close(fd);
    TEST_PASS("test_sys_063_read passed!");
    return 0;
}
