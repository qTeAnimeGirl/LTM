#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Create and unlink queue)
    int mqd = sys_mq_open("/test_q181", O_CREAT | O_RDWR, 0666, NULL);
    TEST_ASSERT(mqd >= 0, "mq_open failed");
    int ret = sys_mq_unlink("/test_q181");
    TEST_ASSERT(ret == 0, "mq_unlink failed");
    sys_close(mqd);

    // Case B: Boundary / Edge Cases (Re-create after unlink)
    mqd = sys_mq_open("/test_q181", O_CREAT | O_RDWR, 0666, NULL);
    TEST_ASSERT(mqd >= 0, "mq_open after unlink failed");
    ret = sys_mq_unlink("/test_q181");
    TEST_ASSERT(ret == 0, "mq_unlink second time failed");
    sys_close(mqd);

    // Case C: Error / Errno Handling (NULL name, bad name, nonexistent)
    ret = sys_mq_unlink(NULL);
    TEST_ASSERT(ret == -EFAULT, "mq_unlink(NULL) must return -EFAULT");

    ret = sys_mq_unlink("no_slash");
    TEST_ASSERT(ret == -EINVAL, "mq_unlink without leading slash must return -EINVAL");

    ret = sys_mq_unlink("/nonexistent_q181");
    TEST_ASSERT(ret == -ENOENT, "mq_unlink(nonexistent) must return -ENOENT");

    TEST_PASS("test_sys_181_mq_unlink passed!");
    return 0;
}
