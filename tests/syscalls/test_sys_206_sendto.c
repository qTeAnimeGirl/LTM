#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int sv[2];
    int ret = sys_socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair failed");

    // Case A: Happy Path (send data to connected pair)
    ssize_t s = sys_sendto(sv[0], "PING", 4, 0, NULL, 0);
    TEST_ASSERT(s == 4, "sendto failed");

    char buf[16];
    ssize_t r = sys_recvfrom(sv[1], buf, sizeof(buf), 0, NULL, NULL);
    TEST_ASSERT(r == 4, "recvfrom failed");

    // Case B: Boundary / Edge Cases (send 0 bytes)
    s = sys_sendto(sv[0], "", 0, 0, NULL, 0);
    TEST_ASSERT(s == 0, "sendto(0 bytes) failed");
    sys_close(sv[0]);
    sys_close(sv[1]);

    // Case C: Error / Errno Handling
    s = sys_sendto(-1, "HI", 2, 0, NULL, 0);
    TEST_ASSERT(s == -EBADF, "sendto(-1) must return -EBADF");
    int fd = sys_socket(AF_UNIX, SOCK_DGRAM, 0);
    s = sys_sendto(fd, NULL, 10, 0, NULL, 0);
    TEST_ASSERT(s == -EFAULT, "sendto(NULL) must return -EFAULT");
    sys_close(fd);

    TEST_PASS("test_sys_206_sendto passed!");
    return 0;
}
