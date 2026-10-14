#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    sys_mq_unlink("/test_q185");
    struct mq_attr init_attr;
    init_attr.mq_flags = 0;
    init_attr.mq_maxmsg = 8;
    init_attr.mq_msgsize = 256;
    init_attr.mq_curmsgs = 0;
    int mqd = sys_mq_open("/test_q185", O_CREAT | O_RDWR, 0666, &init_attr);
    TEST_ASSERT(mqd >= 0, "mq_open failed");

    // Case A: Happy Path (Query attributes)
    struct mq_attr oldattr;
    ltm_memset(&oldattr, 0, sizeof(oldattr));
    int ret = sys_mq_getsetattr(mqd, NULL, &oldattr);
    TEST_ASSERT(ret == 0, "mq_getsetattr query failed");
    TEST_ASSERT(oldattr.mq_maxmsg == 8, "mq_maxmsg mismatch");
    TEST_ASSERT(oldattr.mq_msgsize == 256, "mq_msgsize mismatch");

    // Case B: Boundary / Edge Cases (Set nonblock flag)
    struct mq_attr newattr;
    newattr.mq_flags = 0x800; // O_NONBLOCK
    ret = sys_mq_getsetattr(mqd, &newattr, &oldattr);
    TEST_ASSERT(ret == 0, "mq_getsetattr set failed");

    struct mq_attr check_attr;
    ret = sys_mq_getsetattr(mqd, NULL, &check_attr);
    TEST_ASSERT(ret == 0, "mq_getsetattr second query failed");
    TEST_ASSERT((check_attr.mq_flags & 0x800) != 0, "mq_flags O_NONBLOCK not set");

    // Case C: Error / Errno Handling (Invalid descriptor)
    ret = sys_mq_getsetattr(-1, NULL, &oldattr);
    TEST_ASSERT(ret == -EBADF, "mq_getsetattr(-1) must return -EBADF");

    sys_close(mqd);
    sys_mq_unlink("/test_q185");

    TEST_PASS("test_sys_185_mq_getsetattr passed!");
    return 0;
}
