#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int shmid = sys_shmget(0 /* IPC_PRIVATE */, 16384, 0666 | 01000 /* IPC_CREAT */);
    TEST_ASSERT(shmid >= 0, "shmget failed");

    // Case A: Happy Path (Query segment attributes via IPC_STAT)
    struct shmid_ds ds;
    ltm_memset(&ds, 0, sizeof(ds));
    int ret = sys_shmctl(shmid, 2 /* IPC_STAT */, &ds);
    TEST_ASSERT(ret == 0, "shmctl(IPC_STAT) failed");
    TEST_ASSERT(ds.shm_segsz == 16384, "shm_segsz mismatch");

    // Case B: Boundary / Edge Cases (Set segment permissions via IPC_SET)
    ds.shm_perm.mode = 0600;
    ret = sys_shmctl(shmid, 1 /* IPC_SET */, &ds);
    TEST_ASSERT(ret == 0, "shmctl(IPC_SET) failed");

    // Case C: Error / Errno Handling (NULL buffer on STAT, invalid shmid)
    ret = sys_shmctl(shmid, 2 /* IPC_STAT */, NULL);
    TEST_ASSERT(ret == -EFAULT, "shmctl(IPC_STAT, NULL) must return -EFAULT");

    ret = sys_shmctl(-1, 2 /* IPC_STAT */, &ds);
    TEST_ASSERT(ret == -EINVAL, "shmctl(-1) must return -EINVAL");

    ret = sys_shmctl(shmid, 9999, &ds);
    TEST_ASSERT(ret == -EINVAL, "shmctl(invalid_cmd) must return -EINVAL");

    sys_shmctl(shmid, 0 /* IPC_RMID */, NULL);

    TEST_PASS("test_sys_195_shmctl passed!");
    return 0;
}
