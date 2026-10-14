#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int p[2];
    int ret = sys_pipe2(p, 0);
    TEST_ASSERT(ret == 0, "pipe2 failed");

    // Case A: Happy Path (poll ready descriptor)
    sys_write(p[1], "S", 1);

    uint64_t rfds[16];
    memset(rfds, 0, sizeof(rfds));
    rfds[p[0] / 64] |= (1ULL << (p[0] % 64));

    struct linux_timespec ts = {0, 0};
    ret = sys_pselect6(p[0] + 1, rfds, NULL, NULL, &ts, NULL);
    TEST_ASSERT(ret == 1, "pselect6 must return 1");

    char buf[4];
    sys_read(p[0], buf, 1);

    // Case B: Boundary / Edge Cases (timeout 0 with no events)
    memset(rfds, 0, sizeof(rfds));
    rfds[p[0] / 64] |= (1ULL << (p[0] % 64));
    ret = sys_pselect6(p[0] + 1, rfds, NULL, NULL, &ts, NULL);
    TEST_ASSERT(ret == 0, "pselect6 empty must return 0");

    // Case C: Error / Errno Handling
    ret = sys_pselect6(-1, NULL, NULL, NULL, NULL, NULL);
    TEST_ASSERT(ret == -EINVAL, "pselect6(-1) must return -EINVAL");
    struct linux_timespec bad_ts = {0, 2000000000L};
    ret = sys_pselect6(p[0] + 1, rfds, NULL, NULL, &bad_ts, NULL);
    TEST_ASSERT(ret == -EINVAL, "pselect6(bad_tv_nsec) must return -EINVAL");

    sys_close(p[0]);
    sys_close(p[1]);

    TEST_PASS("test_sys_072_pselect6 passed!");
    return 0;
}
