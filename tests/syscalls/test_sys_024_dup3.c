#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "dup3_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int target_fd = 60;
    int ret = sys_dup3(fd, target_fd, 0);
    TEST_ASSERT(ret == target_fd, "dup3 failed to duplicate to target_fd");
    ssize_t w = sys_write(target_fd, "DUP3", 4);
    TEST_ASSERT(w == 4, "write to target_fd failed");
    sys_close(target_fd);

    // Case B: Boundary / Edge Cases (dup3 with O_CLOEXEC)
    target_fd = 61;
    ret = sys_dup3(fd, target_fd, 0x80000 /* O_CLOEXEC */);
    TEST_ASSERT(ret == target_fd, "dup3 with O_CLOEXEC failed");
    sys_close(target_fd);

    // Case C: Error / Errno Handling
    ret = sys_dup3(fd, fd, 0);
    TEST_ASSERT(ret == -EINVAL, "dup3(fd, fd) must return -EINVAL");
    ret = sys_dup3(-1, 62, 0);
    TEST_ASSERT(ret == -EBADF, "dup3(-1, target) must return -EBADF");

    sys_close(fd);
    sys_unlinkat(AT_FDCWD, tmp, 0);

    TEST_PASS("test_sys_024_dup3 passed!");
    return 0;
}
