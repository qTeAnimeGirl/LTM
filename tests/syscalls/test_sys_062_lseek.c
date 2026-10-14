#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "lseek_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    char dummy[100];
    ltm_memset(dummy, 'X', sizeof(dummy));
    sys_write(fd, dummy, sizeof(dummy));

    // Case A: Happy Path
    off_t pos = sys_lseek(fd, 25, SEEK_SET);
    TEST_ASSERT(pos == 25, "lseek(SEEK_SET) failed");

    // Case B: Boundary / Edge Cases
    pos = sys_lseek(fd, 0, SEEK_END);
    TEST_ASSERT(pos == 100, "lseek(SEEK_END) failed");
    pos = sys_lseek(fd, 0, SEEK_CUR);
    TEST_ASSERT(pos == 100, "lseek(SEEK_CUR) failed");

    // Case C: Error / Errno Handling
    pos = sys_lseek(-1, 0, SEEK_SET);
    TEST_ASSERT(pos == -EBADF, "lseek(-1) must return -EBADF");
    pos = sys_lseek(fd, -10, SEEK_SET);
    TEST_ASSERT(pos == -EINVAL, "lseek(negative) must return -EINVAL");
    pos = sys_lseek(fd, 0, 9999);
    TEST_ASSERT(pos == -EINVAL, "lseek(invalid_whence) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_062_lseek passed!");
    return 0;
}
