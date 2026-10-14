#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Create private semaphore set)
    int semid = sys_semget(0 /* IPC_PRIVATE */, 1, 0666 | IPC_CREAT);
    TEST_ASSERT(semid >= 0, "semget(IPC_PRIVATE) failed");

    // Case B: Boundary / Edge Cases (Create multi-semaphore set)
    int semid2 = sys_semget(0, 3, 0666 | IPC_CREAT);
    TEST_ASSERT(semid2 >= 0, "semget(nsems=3) failed");

    // Case C: Error / Errno Translation
    int ret = sys_semget(0, -1, 0666 | IPC_CREAT);
    TEST_ASSERT(ret == -EINVAL, "semget(negative nsems) must return -EINVAL");

    sys_semctl(semid, 0, IPC_RMID, 0);
    sys_semctl(semid2, 0, IPC_RMID, 0);

    TEST_PASS("test_sys_190_semget passed!");
    return 0;
}
