#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int fd = sys_openat(AT_FDCWD, "/etc/passwd", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "openat /etc/passwd failed");

    struct cachestat_range crange;
    crange.off = 0;
    crange.len = 16384;

    struct cachestat cs;
    ltm_memset(&cs, 0, sizeof(cs));

    // Case A: Happy Path (Query cache statistics for open file)
    int ret = sys_cachestat(fd, &crange, &cs, 0);
    TEST_ASSERT(ret == 0, "cachestat failed");

    // Case B: Boundary / Edge Cases (Offset beyond start)
    crange.off = 4096;
    ret = sys_cachestat(fd, &crange, &cs, 0);
    TEST_ASSERT(ret == 0, "cachestat offset 4096 failed");

    // Case C: Error / Errno Handling (Bad fd -> -EBADF, NULL pointers -> -EFAULT, bad flags -> -EINVAL)
    ret = sys_cachestat(-1, &crange, &cs, 0);
    TEST_ASSERT(ret == -EBADF, "cachestat(-1) must return -EBADF");

    ret = sys_cachestat(fd, NULL, &cs, 0);
    TEST_ASSERT(ret == -EFAULT, "cachestat(NULL range) must return -EFAULT");

    ret = sys_cachestat(fd, &crange, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "cachestat(NULL cs) must return -EFAULT");

    ret = sys_cachestat(fd, &crange, &cs, 999);
    TEST_ASSERT(ret == -EINVAL, "cachestat(bad flags) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_451_cachestat passed!");
    return 0;
}
