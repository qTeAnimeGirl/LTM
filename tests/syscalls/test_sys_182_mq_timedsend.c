#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    sys_mq_unlink("/test_q182");
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 5;
    attr.mq_msgsize = 128;
    attr.mq_curmsgs = 0;
    int mqd = sys_mq_open("/test_q182", O_CREAT | O_RDWR, 0666, &attr);
    TEST_ASSERT(mqd >= 0, "mq_open failed");

    // Case A: Happy Path (Send standard message)
    int ret = sys_mq_timedsend(mqd, "Hello MQ!", 9, 10, NULL);
    TEST_ASSERT(ret == 0, "mq_timedsend failed");

    // Case B: Boundary / Edge Cases (Zero-length message & boundary prios)
    ret = sys_mq_timedsend(mqd, "", 0, 0, NULL);
    TEST_ASSERT(ret == 0, "mq_timedsend zero length failed");

    ret = sys_mq_timedsend(mqd, "Prio100", 7, 100, NULL);
    TEST_ASSERT(ret == 0, "mq_timedsend prio 100 failed");

    // Case C: Error / Errno Handling (NULL ptr, bad fd, msg too large)
    ret = sys_mq_timedsend(mqd, NULL, 10, 0, NULL);
    TEST_ASSERT(ret == -EFAULT, "mq_timedsend(NULL) must return -EFAULT");

    ret = sys_mq_timedsend(-1, "test", 4, 0, NULL);
    TEST_ASSERT(ret == -EBADF, "mq_timedsend(-1) must return -EBADF");

    char huge[256];
    ltm_memset(huge, 'A', sizeof(huge));
    ret = sys_mq_timedsend(mqd, huge, sizeof(huge), 0, NULL);
    TEST_ASSERT(ret == -EMSGSIZE, "mq_timedsend(huge) must return -EMSGSIZE");

    sys_close(mqd);
    sys_mq_unlink("/test_q182");

    TEST_PASS("test_sys_182_mq_timedsend passed!");
    return 0;
}
