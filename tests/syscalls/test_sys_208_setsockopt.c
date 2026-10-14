#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    TEST_ASSERT(fd >= 0, "socket failed");

    // Case A: Happy Path (SO_REUSEADDR)
    int opt = 1;
    int ret = sys_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    TEST_ASSERT(ret == 0, "setsockopt(SO_REUSEADDR) failed");

    // Case B: Boundary / Edge Cases (SO_KEEPALIVE)
    ret = sys_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt));
    TEST_ASSERT(ret == 0, "setsockopt(SO_KEEPALIVE) failed");

    // Case C: Error / Errno Handling
    ret = sys_setsockopt(-1, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    TEST_ASSERT(ret == -EBADF, "setsockopt(-1) must return -EBADF");
    ret = sys_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, NULL, sizeof(opt));
    TEST_ASSERT(ret == -EFAULT, "setsockopt(NULL) must return -EFAULT");
    sys_close(fd);

    TEST_PASS("test_sys_208_setsockopt passed!");
    return 0;
}
