#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Clean up if already exists
    sys_mq_unlink("/test_q180");
    sys_mq_unlink("/test_q180_b");

    // Case A: Happy Path (Create queue with default attributes)
    int mqd = sys_mq_open("/test_q180", O_CREAT | O_RDWR, 0666, NULL);
    TEST_ASSERT(mqd >= 0, "mq_open(O_CREAT) failed");

    // Case B: Boundary / Edge Cases (Create queue with custom attr)
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 5;
    attr.mq_msgsize = 512;
    attr.mq_curmsgs = 0;
    int mqd2 = sys_mq_open("/test_q180_b", O_CREAT | O_RDWR, 0666, &attr);
    TEST_ASSERT(mqd2 >= 0, "mq_open with attr failed");

    // Case C: Error / Errno Handling (NULL name, bad name, O_EXCL on existing, ENOENT without O_CREAT)
    int ret = sys_mq_open(NULL, O_CREAT | O_RDWR, 0666, NULL);
    TEST_ASSERT(ret == -EFAULT, "mq_open(NULL) must return -EFAULT");

    ret = sys_mq_open("invalid_name", O_CREAT | O_RDWR, 0666, NULL);
    TEST_ASSERT(ret == -EINVAL, "mq_open without leading slash must return -EINVAL");

    ret = sys_mq_open("/test_q180", O_CREAT | O_EXCL | O_RDWR, 0666, NULL);
    TEST_ASSERT(ret == -EEXIST, "mq_open(O_CREAT|O_EXCL) on existing must return -EEXIST");

    ret = sys_mq_open("/nonexistent_q", O_RDWR, 0666, NULL);
    TEST_ASSERT(ret == -ENOENT, "mq_open without O_CREAT must return -ENOENT");

    sys_close(mqd);
    sys_close(mqd2);
    sys_mq_unlink("/test_q180");
    sys_mq_unlink("/test_q180_b");

    TEST_PASS("test_sys_180_mq_open passed!");
    return 0;
}
