#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int semid = sys_semget(0, 1, 0666 | IPC_CREAT);
    TEST_ASSERT(semid >= 0, "semget failed");

    // Case A: Happy Path (IPC_RMID)
    int ret = sys_semctl(semid, 0, IPC_RMID, 0);
    TEST_ASSERT(ret == 0, "semctl(IPC_RMID) failed");

    // Case B: Boundary / Edge Cases (IPC_RMID on deleted semaphore)
    ret = sys_semctl(semid, 0, IPC_RMID, 0);
    TEST_ASSERT(ret == -EINVAL, "semctl on deleted semid must return -EINVAL");

    // Case C: Error / Errno Translation
    ret = sys_semctl(-1, 0, IPC_RMID, 0);
    TEST_ASSERT(ret == -EINVAL, "semctl(bad id) must return -EINVAL");

    TEST_PASS("test_sys_191_semctl passed!");
    return 0;
}
