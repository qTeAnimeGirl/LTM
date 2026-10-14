#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int pipefds[2];
    int ret = sys_pipe2(pipefds, 0);
    TEST_ASSERT(ret == 0, "pipe2 failed");

    char data[] = "Hello vmsplice!";
    struct iovec iov;
    iov.iov_base = data;
    iov.iov_len = sizeof(data);

    // Case A: Happy Path (Splice user memory into write end of pipe)
    ret = sys_vmsplice(pipefds[1], &iov, 1, 0);
    TEST_ASSERT(ret == sizeof(data), "vmsplice failed");

    char read_buf[32];
    ret = sys_read(pipefds[0], read_buf, sizeof(read_buf));
    TEST_ASSERT(ret == sizeof(data), "read after vmsplice failed");

    // Case B: Boundary / Edge Cases (nr_segs == 0 -> -EINVAL)
    ret = sys_vmsplice(pipefds[1], &iov, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "vmsplice(nr_segs == 0) must return -EINVAL");

    // Case C: Error / Errno Handling (Bad fd -> -EBADF, NULL iov -> -EFAULT)
    ret = sys_vmsplice(-1, &iov, 1, 0);
    TEST_ASSERT(ret == -EBADF, "vmsplice(-1) must return -EBADF");

    ret = sys_vmsplice(pipefds[1], NULL, 1, 0);
    TEST_ASSERT(ret == -EFAULT, "vmsplice(NULL iov) must return -EFAULT");

    sys_close(pipefds[0]);
    sys_close(pipefds[1]);

    TEST_PASS("test_sys_075_vmsplice passed!");
    return 0;
}
