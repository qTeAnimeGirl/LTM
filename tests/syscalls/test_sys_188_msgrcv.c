#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

struct msgbuf {
    long mtype;
    char mtext[32];
};

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int msqid = sys_msgget(0, 0666 | IPC_CREAT);
    TEST_ASSERT(msqid >= 0, "msgget failed");

    struct msgbuf smsg;
    smsg.mtype = 1;
    ltm_memcpy(smsg.mtext, "hello", 6);
    int ret = sys_msgsnd(msqid, &smsg, 6, 0);
    TEST_ASSERT(ret == 0, "msgsnd failed");

    // Case A: Happy Path (Receive sent message)
    struct msgbuf rmsg;
    rmsg.mtype = 0;
    ssize_t n = sys_msgrcv(msqid, &rmsg, sizeof(rmsg.mtext), 0, 0);
    TEST_ASSERT(n == 6, "msgrcv received wrong length");
    TEST_ASSERT(ltm_strcmp(rmsg.mtext, "hello") == 0, "msgrcv content mismatch");

    // Case B: Boundary / Edge Cases (IPC_NOWAIT on empty queue returns -ENOMSG)
    n = sys_msgrcv(msqid, &rmsg, sizeof(rmsg.mtext), 0, IPC_NOWAIT);
    TEST_ASSERT(n < 0, "msgrcv(NOWAIT) on empty queue should return negative errno");

    // Case C: Error / Errno Translation
    n = sys_msgrcv(msqid, NULL, 32, 0, 0);
    TEST_ASSERT(n == -EFAULT, "msgrcv(NULL) must return -EFAULT");

    n = sys_msgrcv(-1, &rmsg, 32, 0, 0);
    TEST_ASSERT(n == -EINVAL, "msgrcv(bad id) must return -EINVAL");

    sys_msgctl(msqid, IPC_RMID, NULL);

    TEST_PASS("test_sys_188_msgrcv passed!");
    return 0;
}
