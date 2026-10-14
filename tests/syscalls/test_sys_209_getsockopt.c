#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT(fd >= 0, "socket failed");

    // Case A: Happy Path (query SO_TYPE)
    int type = 0;
    unsigned int optlen = sizeof(type);
    int ret = sys_getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &optlen);
    TEST_ASSERT(ret == 0, "getsockopt(SO_TYPE) failed");
    TEST_ASSERT(type == SOCK_STREAM, "SO_TYPE mismatch");

    // Case B: Boundary / Edge Cases (query SO_REUSEADDR)
    int reuse = 0;
    optlen = sizeof(reuse);
    ret = sys_getsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, &optlen);
    TEST_ASSERT(ret == 0, "getsockopt(SO_REUSEADDR) failed");

    // Case C: Error / Errno Handling
    ret = sys_getsockopt(-1, SOL_SOCKET, SO_TYPE, &type, &optlen);
    TEST_ASSERT(ret == -EBADF, "getsockopt(-1) must return -EBADF");
    ret = sys_getsockopt(fd, SOL_SOCKET, SO_TYPE, NULL, &optlen);
    TEST_ASSERT(ret == -EFAULT, "getsockopt(NULL) must return -EFAULT");
    sys_close(fd);

    TEST_PASS("test_sys_209_getsockopt passed!");
    return 0;
}
