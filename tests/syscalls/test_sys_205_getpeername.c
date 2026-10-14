#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int sv[2];
    int ret = sys_socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair failed");

    // Case A: Happy Path (query peername on socketpair)
    struct linux_sockaddr_un sun;
    unsigned int ulen = sizeof(sun);
    ret = sys_getpeername(sv[0], &sun, &ulen);
    TEST_ASSERT(ret == 0, "getpeername(sv[0]) failed");

    // Case B: Boundary / Edge Cases (query peername on sv[1])
    ulen = sizeof(sun);
    ret = sys_getpeername(sv[1], &sun, &ulen);
    TEST_ASSERT(ret == 0, "getpeername(sv[1]) failed");
    sys_close(sv[0]);
    sys_close(sv[1]);

    // Case C: Error / Errno Handling
    ret = sys_getpeername(-1, &sun, &ulen);
    TEST_ASSERT(ret == -EBADF, "getpeername(-1) must return -EBADF");

    int fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    ulen = sizeof(sun);
    ret = sys_getpeername(fd, &sun, &ulen);
    TEST_ASSERT(ret == -ENOTCONN, "getpeername(unconnected) must return -ENOTCONN");
    sys_close(fd);

    TEST_PASS("test_sys_205_getpeername passed!");
    return 0;
}
