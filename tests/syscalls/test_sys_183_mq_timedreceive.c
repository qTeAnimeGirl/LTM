#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    sys_mq_unlink("/test_q183");
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 5;
    attr.mq_msgsize = 128;
    attr.mq_curmsgs = 0;
    int mqd = sys_mq_open("/test_q183", O_CREAT | O_RDWR | 0x800 /* O_NONBLOCK */, 0666, &attr);
    TEST_ASSERT(mqd >= 0, "mq_open failed");

    sys_mq_timedsend(mqd, "MsgToReceive", 12, 42, NULL);

    // Case A: Happy Path (Receive sent message and verify payload + prio)
    char buf[128];
    ltm_memset(buf, 0, sizeof(buf));
    unsigned int prio = 0;
    ssize_t n = sys_mq_timedreceive(mqd, buf, sizeof(buf), &prio, NULL);
    TEST_ASSERT(n == 12, "mq_timedreceive failed to read 12 bytes");
    TEST_ASSERT(ltm_strcmp(buf, "MsgToReceive") == 0, "mq_timedreceive corrupted payload");
    TEST_ASSERT(prio == 42, "mq_timedreceive wrong prio");

    // Case B: Boundary / Edge Cases (NULL prio ptr & non-blocking empty queue EAGAIN)
    sys_mq_timedsend(mqd, "EdgeMsg", 7, 0, NULL);
    n = sys_mq_timedreceive(mqd, buf, sizeof(buf), NULL, NULL);
    TEST_ASSERT(n == 7, "mq_timedreceive with NULL prio failed");

    n = sys_mq_timedreceive(mqd, buf, sizeof(buf), NULL, NULL);
    TEST_ASSERT(n == -EAGAIN, "mq_timedreceive on empty non-blocking queue must return -EAGAIN");

    // Case C: Error / Errno Handling (NULL ptr, buffer too small, bad fd)
    n = sys_mq_timedreceive(mqd, NULL, sizeof(buf), NULL, NULL);
    TEST_ASSERT(n == -EFAULT, "mq_timedreceive(NULL) must return -EFAULT");

    n = sys_mq_timedreceive(mqd, buf, 10 /* less than mq_msgsize 128 */, NULL, NULL);
    TEST_ASSERT(n == -EMSGSIZE, "mq_timedreceive with small buffer must return -EMSGSIZE");

    n = sys_mq_timedreceive(-1, buf, sizeof(buf), NULL, NULL);
    TEST_ASSERT(n == -EBADF, "mq_timedreceive(-1) must return -EBADF");

    sys_close(mqd);
    sys_mq_unlink("/test_q183");

    TEST_PASS("test_sys_183_mq_timedreceive passed!");
    return 0;
}
