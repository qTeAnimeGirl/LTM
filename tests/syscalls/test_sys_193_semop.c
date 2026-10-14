#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int semid = sys_semget(0, 1, 0666 | IPC_CREAT);
    TEST_ASSERT(semid >= 0, "semget failed");

    // Case A: Happy Path (Increment semaphore value)
    struct sembuf op;
    op.sem_num = 0;
    op.sem_op = 1;
    op.sem_flg = 0;
    int ret = sys_semop(semid, &op, 1);
    TEST_ASSERT(ret == 0, "semop(+1) failed");

    // Case B: Boundary / Edge Cases (Decrement semaphore back to 0)
    op.sem_op = -1;
    ret = sys_semop(semid, &op, 1);
    TEST_ASSERT(ret == 0, "semop(-1) failed");

    // Case C: Error / Errno Translation
    ret = sys_semop(semid, NULL, 1);
    TEST_ASSERT(ret == -EFAULT, "semop(NULL sops) must return -EFAULT");

    ret = sys_semop(semid, &op, 0);
    TEST_ASSERT(ret == -EINVAL, "semop(nsops=0) must return -EINVAL");

    ret = sys_semop(-1, &op, 1);
    TEST_ASSERT(ret == -EINVAL, "semop(bad semid) must return -EINVAL");

    sys_semctl(semid, 0, IPC_RMID, 0);

    TEST_PASS("test_sys_193_semop passed!");
    return 0;
}
