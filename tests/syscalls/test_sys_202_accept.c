#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Setup listening AF_UNIX socket
    int lfd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
    TEST_ASSERT(lfd >= 0, "socket failed");

    struct linux_sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    const char *path = "test_accept_sock";
    int i = 0;
    while (path[i]) { sun.sun_path[i] = path[i]; i++; }
    sun.sun_path[i] = '\0';

    sys_unlinkat(AT_FDCWD, path, 0);
    int ret = sys_bind(lfd, &sun, sizeof(sun));
    TEST_ASSERT(ret == 0, "bind failed");
    ret = sys_listen(lfd, 5);
    TEST_ASSERT(ret == 0, "listen failed");

    // Connect client
    int cfd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
    ret = sys_connect(cfd, &sun, sizeof(sun));
    TEST_ASSERT(ret == 0, "connect failed");

    // Case A: Happy Path (accept connection)
    int afd = sys_accept(lfd, NULL, NULL);
    TEST_ASSERT(afd >= 0, "accept failed");
    sys_close(afd);
    sys_close(cfd);

    // Connect second client for Case B
    cfd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
    ret = sys_connect(cfd, &sun, sizeof(sun));
    TEST_ASSERT(ret == 0, "connect 2 failed");

    // Case B: Boundary / Edge Cases (accept with sockaddr buffer)
    struct linux_sockaddr_un cli_addr;
    unsigned int cli_len = sizeof(cli_addr);
    afd = sys_accept(lfd, &cli_addr, &cli_len);
    TEST_ASSERT(afd >= 0, "accept with addr failed");
    sys_close(afd);
    sys_close(cfd);
    sys_close(lfd);
    sys_unlinkat(AT_FDCWD, path, 0);

    // Case C: Error / Errno Handling
    ret = sys_accept(-1, NULL, NULL);
    TEST_ASSERT(ret == -EBADF, "accept(-1) must return -EBADF");

    TEST_PASS("test_sys_202_accept passed!");
    return 0;
}
