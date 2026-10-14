#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (bind and listen)
    int fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT(fd >= 0, "socket failed");

    struct linux_sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = 0;
    sin.sin_addr = 0x0100007f;

    int ret = sys_bind(fd, &sin, sizeof(sin));
    TEST_ASSERT(ret == 0, "bind failed");

    ret = sys_listen(fd, 10);
    TEST_ASSERT(ret == 0, "listen failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases (backlog 128)
    fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    sys_bind(fd, &sin, sizeof(sin));
    ret = sys_listen(fd, 128);
    TEST_ASSERT(ret == 0, "listen(128) failed");
    sys_close(fd);

    // Case C: Error / Errno Handling
    ret = sys_listen(-1, 5);
    TEST_ASSERT(ret == -EBADF, "listen(-1) must return -EBADF");
    int udp_fd = sys_socket(AF_INET, SOCK_DGRAM, 0);
    ret = sys_listen(udp_fd, 5);
    TEST_ASSERT(ret == -EOPNOTSUPP || ret == -EINVAL, "listen(UDP) must return error");
    sys_close(udp_fd);

    TEST_PASS("test_sys_201_listen passed!");
    return 0;
}
