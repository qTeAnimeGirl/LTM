#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Create private message queue)
    int msqid = sys_msgget(0 /* IPC_PRIVATE */, 0666 | IPC_CREAT);
    TEST_ASSERT(msqid >= 0, "msgget(IPC_PRIVATE) failed");

    // Case B: Boundary / Edge Cases (Open with IPC_CREAT | IPC_EXCL)
    int msqid2 = sys_msgget(0 /* IPC_PRIVATE */, 0666 | IPC_CREAT | IPC_EXCL);
    TEST_ASSERT(msqid2 >= 0, "msgget(IPC_PRIVATE, IPC_CREAT|IPC_EXCL) failed");

    // Case C: Error / Errno Translation (Key without IPC_CREAT)
    int ret = sys_msgget(0x123499, 0); // No IPC_CREAT, nonexistent key
    TEST_ASSERT(ret == -ENOENT, "msgget without IPC_CREAT on non-existent key must return -ENOENT");

    sys_msgctl(msqid, IPC_RMID, NULL);
    sys_msgctl(msqid2, IPC_RMID, NULL);

    TEST_PASS("test_sys_186_msgget passed!");
    return 0;
}
