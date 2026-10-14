#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int sv[2];
    int ret = sys_socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair failed");

    ssize_t s = sys_sendto(sv[0], "HELLO", 5, 0, NULL, 0);
    TEST_ASSERT(s == 5, "sendto failed");

    // Case A: Happy Path (recv data from socket)
    char buf[16];
    ssize_t r = sys_recvfrom(sv[1], buf, sizeof(buf), 0, NULL, NULL);
    TEST_ASSERT(r == 5, "recvfrom failed");
    TEST_ASSERT(buf[0] == 'H' && buf[4] == 'O', "payload mismatch");

    // Case B: Boundary / Edge Cases (recv with src_addr buffer)
    s = sys_sendto(sv[0], "TEST", 4, 0, NULL, 0);
    TEST_ASSERT(s == 4, "sendto 2 failed");
    struct linux_sockaddr_un from;
    unsigned int from_len = sizeof(from);
    r = sys_recvfrom(sv[1], buf, sizeof(buf), 0, &from, &from_len);
    TEST_ASSERT(r == 4, "recvfrom with addr failed");
    sys_close(sv[0]);
    sys_close(sv[1]);

    // Case C: Error / Errno Handling
    r = sys_recvfrom(-1, buf, sizeof(buf), 0, NULL, NULL);
    TEST_ASSERT(r == -EBADF, "recvfrom(-1) must return -EBADF");
    int fd = sys_socket(AF_UNIX, SOCK_DGRAM, 0);
    r = sys_recvfrom(fd, NULL, 10, 0, NULL, NULL);
    TEST_ASSERT(r == -EFAULT, "recvfrom(NULL) must return -EFAULT");
    sys_close(fd);

    TEST_PASS("test_sys_207_recvfrom passed!");
    return 0;
}
