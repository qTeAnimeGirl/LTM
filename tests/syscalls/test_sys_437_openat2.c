#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "openat2_test.tmp";
    struct linux_open_how how;
    ltm_memset(&how, 0, sizeof(how));
    how.flags = O_CREAT | O_RDWR | O_TRUNC;
    how.mode = 0644;
    how.resolve = 0;

    // Case A: Happy Path
    int fd = sys_openat2(AT_FDCWD, tmp, &how, sizeof(how));
    TEST_ASSERT(fd >= 0, "openat2 failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases
    how.flags = O_RDONLY;
    how.mode = 0;
    how.resolve = RESOLVE_BENEATH;
    fd = sys_openat2(AT_FDCWD, tmp, &how, sizeof(how));
    TEST_ASSERT(fd >= 0, "openat2(RESOLVE_BENEATH) failed");
    sys_close(fd);

    // Case C: Error / Errno Handling
    int ret = sys_openat2(AT_FDCWD, tmp, &how, sizeof(how) - 1);
    TEST_ASSERT(ret == -EINVAL, "openat2(small_size) must return -EINVAL");
    ret = sys_openat2(AT_FDCWD, tmp, NULL, sizeof(how));
    TEST_ASSERT(ret == -EFAULT, "openat2(NULL_how) must return -EFAULT");
    ret = sys_openat2(AT_FDCWD, NULL, &how, sizeof(how));
    TEST_ASSERT(ret == -EFAULT, "openat2(NULL_path) must return -EFAULT");
    ret = sys_openat2(AT_FDCWD, "nonexistent_openat2", &how, sizeof(how));
    TEST_ASSERT(ret == -ENOENT, "openat2(nonexistent) must return -ENOENT");
    ret = sys_openat2(-999, tmp, &how, sizeof(how));
    TEST_ASSERT(ret == -EBADF, "openat2(bad_dirfd) must return -EBADF");

    TEST_PASS("test_sys_437_openat2 passed!");
    return 0;
}
