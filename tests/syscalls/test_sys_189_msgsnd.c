#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

struct msgbuf {
    long mtype;
    char mtext[16];
};

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int msqid = sys_msgget(0, 0666 | IPC_CREAT);
    TEST_ASSERT(msqid >= 0, "msgget failed");

    // Case A: Happy Path (Send message)
    struct msgbuf smsg;
    smsg.mtype = 2;
    ltm_memcpy(smsg.mtext, "world", 6);
    int ret = sys_msgsnd(msqid, &smsg, 6, 0);
    TEST_ASSERT(ret == 0, "msgsnd failed");

    // Case B: Boundary / Edge Cases (Send zero length text payload)
    smsg.mtype = 3;
    ret = sys_msgsnd(msqid, &smsg, 0, 0);
    TEST_ASSERT(ret == 0, "msgsnd(0 length) failed");

    // Case C: Error / Errno Translation
    ret = sys_msgsnd(msqid, NULL, 6, 0);
    TEST_ASSERT(ret == -EFAULT, "msgsnd(NULL) must return -EFAULT");

    ret = sys_msgsnd(-1, &smsg, 6, 0);
    TEST_ASSERT(ret == -EINVAL, "msgsnd(bad id) must return -EINVAL");

    sys_msgctl(msqid, IPC_RMID, NULL);

    TEST_PASS("test_sys_189_msgsnd passed!");
    return 0;
}
