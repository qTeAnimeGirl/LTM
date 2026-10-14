#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "syncfs_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");

    // Case A: Happy Path
    int ret = sys_syncfs(fd);
    TEST_ASSERT(ret == 0, "syncfs(fd) failed");

    // Case B: Boundary / Edge Cases
    for (int i = 0; i < 3; i++) {
        ret = sys_syncfs(fd);
        TEST_ASSERT(ret == 0, "repeated syncfs failed");
    }

    // Case C: Error / Errno Handling
    ret = sys_syncfs(-1);
    TEST_ASSERT(ret == -EBADF, "syncfs(-1) must return -EBADF");

    sys_close(fd);
    TEST_PASS("test_sys_267_syncfs passed!");
    return 0;
}
