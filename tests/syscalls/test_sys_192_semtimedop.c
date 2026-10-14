#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int semid = sys_semget(0 /* IPC_PRIVATE */, 2, 0666 | IPC_CREAT);
    TEST_ASSERT(semid >= 0, "semget failed");

    // Initialize sem #0 to 1
    sys_semctl(semid, 0, 16 /* SETVAL */, 1);

    // Case A: Happy Path (Semaphore operation with valid timeout)
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = -1; // Decrement
    op.sem_flg = 0;

    struct timespec ts;
    ts.tv_sec = 1;
    ts.tv_nsec = 0;
    int ret = sys_semtimedop(semid, &op, 1, &ts);
    TEST_ASSERT(ret == 0, "semtimedop failed");

    // Case B: Boundary / Edge Cases (Increment back with NULL timeout)
    op.sem_op = 1; // Increment
    ret = sys_semtimedop(semid, &op, 1, NULL);
    TEST_ASSERT(ret == 0, "semtimedop with NULL timeout failed");

    // Case C: Error / Errno Handling (NULL sops, nsops=0, invalid nsec)
    ret = sys_semtimedop(semid, NULL, 1, NULL);
    TEST_ASSERT(ret == -EFAULT, "semtimedop(NULL sops) must return -EFAULT");

    ret = sys_semtimedop(semid, &op, 0, NULL);
    TEST_ASSERT(ret == -EINVAL, "semtimedop(nsops=0) must return -EINVAL");

    ts.tv_nsec = 1000000000;
    ret = sys_semtimedop(semid, &op, 1, &ts);
    TEST_ASSERT(ret == -EINVAL, "semtimedop(invalid nsec) must return -EINVAL");

    ret = sys_semtimedop(-1, &op, 1, NULL);
    TEST_ASSERT(ret == -EINVAL, "semtimedop(-1) must return -EINVAL");

    sys_semctl(semid, 0, 0 /* IPC_RMID */, 0);

    TEST_PASS("test_sys_192_semtimedop passed!");
    return 0;
}
