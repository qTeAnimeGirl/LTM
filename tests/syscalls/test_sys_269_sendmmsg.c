#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int sv[2];
    int ret = sys_socketpair(1 /* AF_UNIX */, 1 /* SOCK_STREAM */, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair failed");

    // Case A: Happy Path (Send 2 messages via sendmmsg)
    char data1[] = "Hello";
    char data2[] = "World";
    struct iovec iov[2];
    iov[0].iov_base = data1;
    iov[0].iov_len = 5;
    iov[1].iov_base = data2;
    iov[1].iov_len = 5;

    struct mmsghdr msgvec[2];
    ltm_memset(msgvec, 0, sizeof(msgvec));
    msgvec[0].msg_hdr.msg_iov = &iov[0];
    msgvec[0].msg_hdr.msg_iovlen = 1;
    msgvec[1].msg_hdr.msg_iov = &iov[1];
    msgvec[1].msg_hdr.msg_iovlen = 1;

    ret = sys_sendmmsg(sv[0], msgvec, 2, 0);
    TEST_ASSERT(ret == 2, "sendmmsg failed to send 2 messages");

    // Case B: Boundary / Edge Cases (vlen = 0 returns 0)
    ret = sys_sendmmsg(sv[0], msgvec, 0, 0);
    TEST_ASSERT(ret == 0, "sendmmsg(vlen=0) must return 0");

    // Case C: Error / Errno Handling (NULL msgvec & vlen > 1024)
    ret = sys_sendmmsg(sv[0], NULL, 1, 0);
    TEST_ASSERT(ret == -EFAULT, "sendmmsg(NULL) must return -EFAULT");

    ret = sys_sendmmsg(sv[0], msgvec, 2048, 0);
    TEST_ASSERT(ret == -EINVAL, "sendmmsg(vlen > 1024) must return -EINVAL");

    sys_close(sv[0]);
    sys_close(sv[1]);

    TEST_PASS("test_sys_269_sendmmsg passed!");
    return 0;
}
