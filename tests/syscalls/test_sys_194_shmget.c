#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Allocate private shared memory segment)
    int shmid = sys_shmget(0 /* IPC_PRIVATE */, 16384, 0666 | 01000 /* IPC_CREAT */);
    TEST_ASSERT(shmid >= 0, "shmget(IPC_PRIVATE, 16384) failed");

    // Case B: Boundary / Edge Cases (Allocate with IPC_CREAT | IPC_EXCL)
    int shmid2 = sys_shmget(0 /* IPC_PRIVATE */, 16384, 0666 | 01000 | 02000 /* IPC_CREAT|IPC_EXCL */);
    TEST_ASSERT(shmid2 >= 0, "shmget with IPC_EXCL failed");

    // Case C: Error / Errno Handling (Size 0 on create & nonexistent key without create)
    int ret = sys_shmget(0 /* IPC_PRIVATE */, 0, 0666 | 01000);
    TEST_ASSERT(ret == -EINVAL, "shmget(size=0, IPC_CREAT) must return -EINVAL");

    ret = sys_shmget(0x987654, 16384, 0); // No IPC_CREAT
    TEST_ASSERT(ret == -ENOENT, "shmget without IPC_CREAT on non-existent key must return -ENOENT");

    sys_shmctl(shmid, 0 /* IPC_RMID */, NULL);
    sys_shmctl(shmid2, 0 /* IPC_RMID */, NULL);

    TEST_PASS("test_sys_194_shmget passed!");
    return 0;
}
