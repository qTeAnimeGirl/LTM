#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char buf[64];
    struct iovec iov;
    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    // Case A: Happy Path (Valid process_madvise call on valid descriptor)
    int ret = sys_process_madvise(1, &iov, 1, 0 /* MADV_NORMAL */, 0);
    TEST_ASSERT(ret == 0, "process_madvise failed");

    // Case B: Boundary / Edge Cases (MADV_WILLNEED)
    ret = sys_process_madvise(1, &iov, 1, 3 /* MADV_WILLNEED */, 0);
    TEST_ASSERT(ret == 0, "process_madvise(WILLNEED) failed");

    // Case C: Error / Errno Handling (NULL iovec -> -EFAULT, vlen=0 -> -EINVAL, bad flags -> -EINVAL)
    ret = sys_process_madvise(1, NULL, 1, 0, 0);
    TEST_ASSERT(ret == -EFAULT, "process_madvise(NULL iov) must return -EFAULT");

    ret = sys_process_madvise(1, &iov, 0, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "process_madvise(vlen=0) must return -EINVAL");

    ret = sys_process_madvise(1, &iov, 1, 0, 999);
    TEST_ASSERT(ret == -EINVAL, "process_madvise(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_440_process_madvise passed!");
    return 0;
}
