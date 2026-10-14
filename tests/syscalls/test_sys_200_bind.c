#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (bind AF_INET to 127.0.0.1:0 ephemeral)
    int fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT(fd >= 0, "socket creation failed");

    struct linux_sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = 0; // ephemeral port
    sin.sin_addr = 0x0100007f; // 127.0.0.1

    int ret = sys_bind(fd, &sin, sizeof(sin));
    TEST_ASSERT(ret == 0, "bind(127.0.0.1:0) failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases (bind AF_UNIX local socket)
    fd = sys_socket(AF_UNIX, SOCK_DGRAM, 0);
    TEST_ASSERT(fd >= 0, "unix socket creation failed");

    struct linux_sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    const char *path = "test_bind_sock";
    int i = 0;
    while (path[i]) { sun.sun_path[i] = path[i]; i++; }
    sun.sun_path[i] = '\0';

    sys_unlinkat(AT_FDCWD, path, 0);
    ret = sys_bind(fd, &sun, sizeof(sun));
    TEST_ASSERT(ret == 0, "bind(AF_UNIX) failed");
    sys_close(fd);
    sys_unlinkat(AT_FDCWD, path, 0);

    // Case C: Error / Errno Handling (invalid fd or NULL addr)
    ret = sys_bind(-1, &sin, sizeof(sin));
    TEST_ASSERT(ret == -EBADF, "bind(-1) must return -EBADF");
    fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    ret = sys_bind(fd, NULL, 0);
    TEST_ASSERT(ret == -EINVAL || ret == -EFAULT, "bind(NULL) must return error");
    sys_close(fd);

    TEST_PASS("test_sys_200_bind passed!");
    return 0;
}
