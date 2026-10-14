#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int sv[2];
    int ret = sys_socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair failed");

    sys_write(sv[0], "RECV_MSG", 8);

    // Case A: Happy Path (recv message vector)
    char buf[32];
    struct linux_iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    struct linux_msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    ssize_t r = sys_recvmsg(sv[1], &msg, 0);
    TEST_ASSERT(r == 8, "recvmsg failed");
    TEST_ASSERT(buf[0] == 'R' && buf[7] == 'G', "content mismatch");

    // Case B: Boundary / Edge Cases (recvmsg with name buffer)
    sys_write(sv[0], "MORE", 4);
    struct linux_sockaddr_un from;
    msg.msg_name = &from;
    msg.msg_namelen = sizeof(from);
    r = sys_recvmsg(sv[1], &msg, 0);
    TEST_ASSERT(r == 4, "recvmsg with name failed");
    sys_close(sv[0]);
    sys_close(sv[1]);

    // Case C: Error / Errno Handling
    r = sys_recvmsg(-1, &msg, 0);
    TEST_ASSERT(r == -EBADF, "recvmsg(-1) must return -EBADF");
    int fd = sys_socket(AF_UNIX, SOCK_DGRAM, 0);
    r = sys_recvmsg(fd, NULL, 0);
    TEST_ASSERT(r == -EINVAL || r == -EFAULT, "recvmsg(NULL) must return error");
    sys_close(fd);

    TEST_PASS("test_sys_212_recvmsg passed!");
    return 0;
}
