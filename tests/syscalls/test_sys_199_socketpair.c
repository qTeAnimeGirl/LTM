#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (AF_UNIX stream socketpair)
    int sv[2];
    int ret = sys_socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair(AF_UNIX, SOCK_STREAM) failed");
    TEST_ASSERT(sv[0] >= 0 && sv[1] >= 0, "socketpair returned invalid fds");

    ssize_t w = sys_write(sv[0], "SP", 2);
    TEST_ASSERT(w == 2, "socketpair write failed");
    char buf[4];
    ssize_t r = sys_read(sv[1], buf, 2);
    TEST_ASSERT(r == 2, "socketpair read failed");
    sys_close(sv[0]);
    sys_close(sv[1]);

    // Case B: Boundary / Edge Cases (AF_UNIX DGRAM with CLOEXEC)
    ret = sys_socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, sv);
    TEST_ASSERT(ret == 0, "socketpair(AF_UNIX, SOCK_DGRAM|CLOEXEC) failed");
    sys_close(sv[0]);
    sys_close(sv[1]);

    // Case C: Error / Errno Handling (NULL sv)
    ret = sys_socketpair(AF_UNIX, SOCK_STREAM, 0, NULL);
    TEST_ASSERT(ret == -EINVAL || ret == -EFAULT, "socketpair(NULL) must return error");

    TEST_PASS("test_sys_199_socketpair passed!");
    return 0;
}
