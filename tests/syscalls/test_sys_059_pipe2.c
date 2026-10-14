#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int pfd[2];

    // Case A: Happy Path
    int ret = sys_pipe2(pfd, 0);
    TEST_ASSERT(ret == 0, "pipe2 failed");
    ssize_t w = sys_write(pfd[1], "P2", 2);
    TEST_ASSERT(w == 2, "pipe write failed");
    char buf[4];
    ssize_t r = sys_read(pfd[0], buf, 2);
    TEST_ASSERT(r == 2, "pipe read failed");
    sys_close(pfd[0]);
    sys_close(pfd[1]);

    // Case B: Boundary / Edge Cases (O_CLOEXEC)
    ret = sys_pipe2(pfd, 0x80000 /* O_CLOEXEC */);
    TEST_ASSERT(ret == 0, "pipe2 with O_CLOEXEC failed");
    sys_close(pfd[0]);
    sys_close(pfd[1]);

    // Case C: Error / Errno Handling
    ret = sys_pipe2(NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "pipe2(NULL) must return -EFAULT");

    TEST_PASS("test_sys_059_pipe2 passed!");
    return 0;
}
