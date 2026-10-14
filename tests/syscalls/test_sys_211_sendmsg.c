#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int sv[2];
    int ret = sys_socketpair(AF_UNIX, SOCK_DGRAM, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair failed");

    // Case A: Happy Path (send message vector)
    char payload[] = "MSG_DATA";
    struct linux_iovec iov;
    iov.iov_base = payload;
    iov.iov_len = sizeof(payload);

    struct linux_msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    ssize_t s = sys_sendmsg(sv[0], &msg, 0);
    TEST_ASSERT(s == (ssize_t)sizeof(payload), "sendmsg failed");

    char buf[32];
    ssize_t r = sys_read(sv[1], buf, sizeof(buf));
    TEST_ASSERT(r == (ssize_t)sizeof(payload), "read failed");

    // Case B: Boundary / Edge Cases (send 0 bytes iov)
    iov.iov_len = 0;
    s = sys_sendmsg(sv[0], &msg, 0);
    TEST_ASSERT(s == 0, "sendmsg(0) failed");
    sys_close(sv[0]);
    sys_close(sv[1]);

    // Case C: Error / Errno Handling
    s = sys_sendmsg(-1, &msg, 0);
    TEST_ASSERT(s == -EBADF, "sendmsg(-1) must return -EBADF");
    int fd = sys_socket(AF_UNIX, SOCK_DGRAM, 0);
    s = sys_sendmsg(fd, NULL, 0);
    TEST_ASSERT(s == -EINVAL || s == -EFAULT, "sendmsg(NULL) must return error");
    sys_close(fd);

    TEST_PASS("test_sys_211_sendmsg passed!");
    return 0;
}
