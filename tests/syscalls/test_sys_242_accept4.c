#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path / Bad fd return
    int ret = sys_accept4(-1, NULL, NULL, 0);
    TEST_ASSERT(ret == -EBADF, "accept4(-1) must return -EBADF");

    // Case B: Boundary flags
    ret = sys_accept4(-1, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    TEST_ASSERT(ret == -EBADF, "accept4(flags) on -1 must return -EBADF");

    // Case C: Regular file descriptor
    const char *tmp = "accept4_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    ret = sys_accept4(fd, NULL, NULL, 0);
    TEST_ASSERT(ret == -ENOTSOCK || ret == -EINVAL, "accept4(file) must return -ENOTSOCK");
    ret = sys_accept4(fd, NULL, NULL, 0xffffff);
    TEST_ASSERT(ret == -EINVAL || ret == -ENOTSOCK, "accept4(invalid_flags) must return -EINVAL");
    sys_close(fd);

    TEST_PASS("test_sys_242_accept4 passed!");
    return 0;
}
