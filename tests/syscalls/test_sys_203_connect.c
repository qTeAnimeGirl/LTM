#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int lfd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
    TEST_ASSERT(lfd >= 0, "socket failed");

    struct linux_sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    const char *path = "test_conn_sock";
    int i = 0;
    while (path[i]) { sun.sun_path[i] = path[i]; i++; }
    sun.sun_path[i] = '\0';

    sys_unlinkat(AT_FDCWD, path, 0);
    int ret = sys_bind(lfd, &sun, sizeof(sun));
    TEST_ASSERT(ret == 0, "bind failed");
    ret = sys_listen(lfd, 5);
    TEST_ASSERT(ret == 0, "listen failed");

    // Case A: Happy Path (connect to listening server)
    int cfd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
    ret = sys_connect(cfd, &sun, sizeof(sun));
    TEST_ASSERT(ret == 0, "connect failed");

    int afd = sys_accept(lfd, NULL, NULL);
    TEST_ASSERT(afd >= 0, "accept failed");
    sys_close(afd);
    sys_close(cfd);

    // Case B: Boundary / Edge Cases (connect with nonblock socket)
    cfd = sys_socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
    ret = sys_connect(cfd, &sun, sizeof(sun));
    TEST_ASSERT(ret == 0 || ret == -EINPROGRESS, "connect nonblock failed");
    sys_close(cfd);
    sys_close(lfd);
    sys_unlinkat(AT_FDCWD, path, 0);

    // Case C: Error / Errno Handling
    ret = sys_connect(-1, &sun, sizeof(sun));
    TEST_ASSERT(ret == -EBADF, "connect(-1) must return -EBADF");
    cfd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
    ret = sys_connect(cfd, NULL, 0);
    TEST_ASSERT(ret == -EINVAL || ret == -EFAULT, "connect(NULL) must return error");
    sys_close(cfd);

    TEST_PASS("test_sys_203_connect passed!");
    return 0;
}
