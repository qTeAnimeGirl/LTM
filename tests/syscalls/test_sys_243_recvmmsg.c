#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int sv[2];
    int ret = sys_socketpair(1 /* AF_UNIX */, 1 /* SOCK_STREAM */, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair failed");

    // Send two messages into sv[1]
    sys_write(sv[1], "MsgA", 4);
    sys_write(sv[1], "MsgB", 4);

    // Case A: Happy Path (Receive two messages via recvmmsg)
    char buf1[16], buf2[16];
    struct iovec iov[2];
    iov[0].iov_base = buf1;
    iov[0].iov_len = 4;
    iov[1].iov_base = buf2;
    iov[1].iov_len = 4;

    struct mmsghdr msgvec[2];
    ltm_memset(msgvec, 0, sizeof(msgvec));
    msgvec[0].msg_hdr.msg_iov = &iov[0];
    msgvec[0].msg_hdr.msg_iovlen = 1;
    msgvec[1].msg_hdr.msg_iov = &iov[1];
    msgvec[1].msg_hdr.msg_iovlen = 1;

    ret = sys_recvmmsg(sv[0], msgvec, 2, 0, NULL);
    TEST_ASSERT(ret >= 1, "recvmmsg failed to receive messages");

    // Case B: Boundary / Edge Cases (vlen = 0 returns 0)
    ret = sys_recvmmsg(sv[0], msgvec, 0, 0, NULL);
    TEST_ASSERT(ret == 0, "recvmmsg(vlen=0) must return 0");

    // Case C: Error / Errno Handling (NULL msgvec & vlen > 1024)
    ret = sys_recvmmsg(sv[0], NULL, 1, 0, NULL);
    TEST_ASSERT(ret == -EFAULT, "recvmmsg(NULL) must return -EFAULT");

    ret = sys_recvmmsg(sv[0], msgvec, 2048, 0, NULL);
    TEST_ASSERT(ret == -EINVAL, "recvmmsg(vlen > 1024) must return -EINVAL");

    sys_close(sv[0]);
    sys_close(sv[1]);

    TEST_PASS("test_sys_243_recvmmsg passed!");
    return 0;
}
