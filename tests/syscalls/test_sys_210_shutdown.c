#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int sv[2];
    int ret = sys_socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair failed");

    // Case A: Happy Path (SHUT_WR)
    ret = sys_shutdown(sv[0], SHUT_WR);
    TEST_ASSERT(ret == 0, "shutdown(SHUT_WR) failed");

    sys_close(sv[0]);
    sys_close(sv[1]);

    // Case B: Boundary / Edge Cases (SHUT_RDWR on fresh pair)
    int sv2[2];
    ret = sys_socketpair(AF_UNIX, SOCK_STREAM, 0, sv2);
    TEST_ASSERT(ret == 0, "socketpair 2 failed");
    ret = sys_shutdown(sv2[0], SHUT_RDWR);
    TEST_ASSERT(ret == 0, "shutdown(SHUT_RDWR) failed");
    sys_close(sv2[0]);
    sys_close(sv2[1]);


    // Case C: Error / Errno Handling
    ret = sys_shutdown(-1, SHUT_RD);
    TEST_ASSERT(ret == -EBADF, "shutdown(-1) must return -EBADF");
    int fd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
    ret = sys_shutdown(fd, 999);
    TEST_ASSERT(ret == -EINVAL, "shutdown(invalid_how) must return -EINVAL");
    sys_close(fd);

    TEST_PASS("test_sys_210_shutdown passed!");
    return 0;
}
