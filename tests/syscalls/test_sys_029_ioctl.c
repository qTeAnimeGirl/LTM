#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int p[2];
    int ret = sys_pipe2(p, 0);
    TEST_ASSERT(ret == 0, "pipe2 failed");

    // Case A: Happy Path (FIONREAD = 0x541b)
    int nbytes = -1;
    ret = sys_ioctl(p[0], 0x541b, &nbytes);
    TEST_ASSERT(ret == 0, "ioctl(FIONREAD) failed");
    TEST_ASSERT(nbytes == 0, "initial bytes must be 0");

    // Case B: Boundary / Edge Cases (FIONREAD after write)
    sys_write(p[1], "IOCTL", 5);
    ret = sys_ioctl(p[0], 0x541b, &nbytes);
    TEST_ASSERT(ret == 0, "ioctl(FIONREAD) failed");
    TEST_ASSERT(nbytes == 5, "bytes must be 5");
    char buf[8];
    sys_read(p[0], buf, 5);

    // Case C: Error / Errno Handling
    ret = sys_ioctl(-1, 0x541b, &nbytes);
    TEST_ASSERT(ret == -EBADF, "ioctl(-1) must return -EBADF");
    ret = sys_ioctl(p[0], 0x999999, &nbytes);
    TEST_ASSERT(ret == -ENOTTY, "ioctl(unknown) must return -ENOTTY");

    sys_close(p[0]);
    sys_close(p[1]);

    TEST_PASS("test_sys_029_ioctl passed!");
    return 0;
}
