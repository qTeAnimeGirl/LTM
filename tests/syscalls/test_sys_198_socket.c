#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (AF_UNIX stream socket)
    int fd = sys_socket(AF_UNIX, SOCK_STREAM, 0);
    TEST_ASSERT(fd >= 0, "socket(AF_UNIX, SOCK_STREAM) failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases (AF_INET with CLOEXEC & NONBLOCK)
    fd = sys_socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    TEST_ASSERT(fd >= 0, "socket(AF_INET, SOCK_DGRAM) failed");
    sys_close(fd);

    // Case C: Error / Errno Handling (invalid domain)
    int ret = sys_socket(9999, SOCK_STREAM, 0);
    TEST_ASSERT(ret == -EAFNOSUPPORT || ret == -EINVAL, "socket(invalid_domain) must return error");

    TEST_PASS("test_sys_198_socket passed!");
    return 0;
}
