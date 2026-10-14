#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int shmid = sys_shmget(0 /* IPC_PRIVATE */, 16384, 0666 | 01000 /* IPC_CREAT */);
    TEST_ASSERT(shmid >= 0, "shmget failed");

    // Case A: Happy Path (Attach segment at kernel-chosen address)
    void *addr = sys_shmat(shmid, NULL, 0);
    TEST_ASSERT(addr != (void *)-1 && addr != NULL, "shmat(NULL) failed");

    // Write and verify memory integrity
    char *ptr = (char *)addr;
    ptr[0] = 'L';
    ptr[1] = 'T';
    ptr[2] = 'M';
    ptr[3] = '\0';
    TEST_ASSERT(ptr[0] == 'L' && ptr[1] == 'T' && ptr[2] == 'M', "shmat memory verification failed");

    // Case B: Boundary / Edge Cases (Attach second view with SHM_RDONLY)
    void *addr_ro = sys_shmat(shmid, NULL, 010000 /* SHM_RDONLY */);
    TEST_ASSERT(addr_ro != (void *)-1, "shmat(SHM_RDONLY) failed");
    char *ptr_ro = (char *)addr_ro;
    TEST_ASSERT(ptr_ro[0] == 'L', "shmat read-only view mismatch");

    // Case C: Error / Errno Handling (Invalid shmid & unaligned shmaddr)
    void *bad_ret = sys_shmat(-1, NULL, 0);
    TEST_ASSERT(bad_ret == (void *)-EINVAL, "shmat(-1) must return -EINVAL");

    bad_ret = sys_shmat(shmid, (void *)0x12345, 0); // Unaligned address without SHM_RND
    TEST_ASSERT(bad_ret == (void *)-EINVAL, "shmat with unaligned address must return -EINVAL");

    sys_shmdt(addr);
    sys_shmdt(addr_ro);
    sys_shmctl(shmid, 0 /* IPC_RMID */, NULL);

    TEST_PASS("test_sys_196_shmat passed!");
    return 0;
}
