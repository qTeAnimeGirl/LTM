#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fallocate_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_fallocate(fd, 0, 0, 4096);
    TEST_ASSERT(ret == 0, "fallocate(0, 0, 4096) failed");

    // Case B: Boundary / Edge Cases
    ret = sys_fallocate(fd, FALLOC_FL_KEEP_SIZE, 0, 8192);
    TEST_ASSERT(ret == 0, "fallocate(KEEP_SIZE) failed");

    // Case C: Error / Errno Handling
    ret = sys_fallocate(-1, 0, 0, 4096);
    TEST_ASSERT(ret == -EBADF, "fallocate(-1) must return -EBADF");
    ret = sys_fallocate(fd, 0, 0, -100);
    TEST_ASSERT(ret == -EINVAL, "fallocate(len<=0) must return -EINVAL");
    ret = sys_fallocate(fd, 0, -5, 4096);
    TEST_ASSERT(ret == -EINVAL, "fallocate(offset<0) must return -EINVAL");

    sys_close(fd);
    TEST_PASS("test_sys_047_fallocate passed!");
    return 0;
}
