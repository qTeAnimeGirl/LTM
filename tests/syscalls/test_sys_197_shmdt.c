#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int shmid = sys_shmget(0 /* IPC_PRIVATE */, 16384, 0666 | 01000 /* IPC_CREAT */);
    TEST_ASSERT(shmid >= 0, "shmget failed");

    // Case A: Happy Path (Attach and detach shared memory segment)
    void *addr = sys_shmat(shmid, NULL, 0);
    TEST_ASSERT(addr != (void *)-1, "shmat failed");

    int ret = sys_shmdt(addr);
    TEST_ASSERT(ret == 0, "shmdt failed");

    // Case B: Boundary / Edge Cases (Attach twice, detach twice)
    void *addr1 = sys_shmat(shmid, NULL, 0);
    void *addr2 = sys_shmat(shmid, NULL, 0);
    TEST_ASSERT(addr1 != (void *)-1 && addr2 != (void *)-1, "shmat dual attach failed");

    ret = sys_shmdt(addr1);
    TEST_ASSERT(ret == 0, "shmdt(addr1) failed");
    ret = sys_shmdt(addr2);
    TEST_ASSERT(ret == 0, "shmdt(addr2) failed");

    // Case C: Error / Errno Handling (NULL ptr and unaligned address)
    ret = sys_shmdt(NULL);
    TEST_ASSERT(ret == -EINVAL, "shmdt(NULL) must return -EINVAL");

    ret = sys_shmdt((void *)0x7777);
    TEST_ASSERT(ret == -EINVAL, "shmdt(unaligned) must return -EINVAL");

    sys_shmctl(shmid, 0 /* IPC_RMID */, NULL);

    TEST_PASS("test_sys_197_shmdt passed!");
    return 0;
}
