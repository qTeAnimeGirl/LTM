#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    sys_mq_unlink("/test_q184");
    int mqd = sys_mq_open("/test_q184", O_CREAT | O_RDWR, 0666, NULL);
    TEST_ASSERT(mqd >= 0, "mq_open failed");

    // Case A: Happy Path (Deregister notification with NULL)
    int ret = sys_mq_notify(mqd, NULL);
    TEST_ASSERT(ret == 0, "mq_notify(NULL) failed");

    // Case B: Boundary / Edge Cases (Repeated NULL notification)
    ret = sys_mq_notify(mqd, NULL);
    TEST_ASSERT(ret == 0, "mq_notify repeated call failed");

    // Case C: Error / Errno Handling (Invalid descriptor)
    ret = sys_mq_notify(-1, NULL);
    TEST_ASSERT(ret == -EBADF, "mq_notify(-1) must return -EBADF");

    sys_close(mqd);
    sys_mq_unlink("/test_q184");

    TEST_PASS("test_sys_184_mq_notify passed!");
    return 0;
}
