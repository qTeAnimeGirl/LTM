#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int p1[2], p2[2];
    int ret = sys_pipe2(p1, 0);
    TEST_ASSERT(ret == 0, "pipe2(p1) failed");
    ret = sys_pipe2(p2, 0);
    TEST_ASSERT(ret == 0, "pipe2(p2) failed");

    // Case A: Happy Path
    sys_write(p1[1], "tee_data", 8);
    ssize_t tret = sys_tee(p1[0], p2[1], 8, 0);
    TEST_ASSERT(tret == 8, "tee failed to duplicate data");

    char buf[16];
    ltm_memset(buf, 0, sizeof(buf));
    sys_read(p2[0], buf, 8);
    TEST_ASSERT(ltm_memcmp(buf, "tee_data", 8) == 0, "tee data mismatch");

    // Case B: Boundary / Edge Cases (len=0)
    tret = sys_tee(p1[0], p2[1], 0, 0);
    TEST_ASSERT(tret == 0, "tee(len=0) must return 0");

    // Case C: Error / Errno Translation
    tret = sys_tee(-1, p2[1], 8, 0);
    TEST_ASSERT(tret == -EBADF, "tee(in=-1) must return -EBADF");
    tret = sys_tee(p1[0], -1, 8, 0);
    TEST_ASSERT(tret == -EBADF, "tee(out=-1) must return -EBADF");

    sys_close(p1[0]);
    sys_close(p1[1]);
    sys_close(p2[0]);
    sys_close(p2[1]);

    TEST_PASS("test_sys_077_tee passed!");
    return 0;
}
