#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int msqid = sys_msgget(0, 0666 | IPC_CREAT);
    TEST_ASSERT(msqid >= 0, "msgget failed");

    // Case A: Happy Path (IPC_RMID)
    int ret = sys_msgctl(msqid, IPC_RMID, NULL);
    TEST_ASSERT(ret == 0, "msgctl(IPC_RMID) failed");

    // Case B: Boundary / Edge Cases (IPC_RMID on already deleted queue)
    ret = sys_msgctl(msqid, IPC_RMID, NULL);
    TEST_ASSERT(ret == -EINVAL, "msgctl(IPC_RMID) on deleted queue must return -EINVAL");

    // Case C: Error / Errno Translation
    ret = sys_msgctl(-1, IPC_RMID, NULL);
    TEST_ASSERT(ret == -EINVAL, "msgctl(bad id) must return -EINVAL");

    TEST_PASS("test_sys_187_msgctl passed!");
    return 0;
}
