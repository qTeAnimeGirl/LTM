#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT(fd >= 0, "socket failed");

    struct linux_sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = 0;
    sin.sin_addr = 0x0100007f;
    int ret = sys_bind(fd, &sin, sizeof(sin));
    TEST_ASSERT(ret == 0, "bind failed");

    // Case A: Happy Path (query bound sockaddr)
    struct linux_sockaddr_in out_sin;
    unsigned int slen = sizeof(out_sin);
    ret = sys_getsockname(fd, &out_sin, &slen);
    TEST_ASSERT(ret == 0, "getsockname failed");
    TEST_ASSERT(out_sin.sin_family == AF_INET, "sin_family mismatch");

    // Case B: Boundary / Edge Cases (query on socketpair)
    int sv[2];
    ret = sys_socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair failed");
    struct linux_sockaddr_un sun;
    unsigned int ulen = sizeof(sun);
    ret = sys_getsockname(sv[0], &sun, &ulen);
    TEST_ASSERT(ret == 0, "getsockname(socketpair) failed");
    sys_close(sv[0]);
    sys_close(sv[1]);
    sys_close(fd);

    // Case C: Error / Errno Handling
    ret = sys_getsockname(-1, &out_sin, &slen);
    TEST_ASSERT(ret == -EBADF, "getsockname(-1) must return -EBADF");
    fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    ret = sys_getsockname(fd, NULL, NULL);
    TEST_ASSERT(ret == -EINVAL || ret == -EFAULT, "getsockname(NULL) must return error");
    sys_close(fd);

    TEST_PASS("test_sys_204_getsockname passed!");
    return 0;
}
