#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int p[2];
    int ret = sys_pipe2(p, 0);
    TEST_ASSERT(ret == 0, "pipe2 failed");

    sys_write(p[1], "P", 1);

    // Case A: Happy Path (poll ready fd)
    struct linux_pollfd pfd;
    pfd.fd = p[0];
    pfd.events = 1; // POLLIN
    pfd.revents = 0;

    struct linux_timespec ts = {0, 0};
    ret = sys_ppoll(&pfd, 1, &ts, NULL);
    TEST_ASSERT(ret == 1, "ppoll must return 1");
    TEST_ASSERT(pfd.revents & 1, "POLLIN bit missing");

    char buf[4];
    sys_read(p[0], buf, 1);

    // Case B: Boundary / Edge Cases (no events, immediate return)
    pfd.revents = 0;
    ret = sys_ppoll(&pfd, 1, &ts, NULL);
    TEST_ASSERT(ret == 0, "ppoll empty must return 0");

    // Case C: Error / Errno Handling
    ret = sys_ppoll(NULL, 1, &ts, NULL);
    TEST_ASSERT(ret == -EFAULT, "ppoll(NULL fds) must return -EFAULT");
    struct linux_timespec bad_ts = {0, 2000000000L};
    ret = sys_ppoll(&pfd, 1, &bad_ts, NULL);
    TEST_ASSERT(ret == -EINVAL, "ppoll(bad_tv_nsec) must return -EINVAL");

    sys_close(p[0]);
    sys_close(p[1]);

    TEST_PASS("test_sys_073_ppoll passed!");
    return 0;
}
