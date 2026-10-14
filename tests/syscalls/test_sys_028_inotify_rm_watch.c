#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int ifd = sys_inotify_init1(0);
    TEST_ASSERT(ifd >= 0, "inotify_init1 failed");

    const char *tmp_file = "tmp_sys_028_inotify.tmp";
    int fd = sys_openat(AT_FDCWD, tmp_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    int wd = sys_inotify_add_watch(ifd, tmp_file, IN_MODIFY);
    TEST_ASSERT(wd >= 1, "inotify_add_watch failed");

    // Case A: Happy Path (Remove watch)
    int ret = sys_inotify_rm_watch(ifd, wd);
    TEST_ASSERT(ret == 0, "inotify_rm_watch failed");

    // Case B: Boundary / Edge Cases (Remove already removed wd)
    ret = sys_inotify_rm_watch(ifd, wd);
    TEST_ASSERT(ret == -EINVAL, "inotify_rm_watch on removed wd must return -EINVAL");

    // Case C: Error / Errno Translation
    ret = sys_inotify_rm_watch(-1, wd);
    TEST_ASSERT(ret == -EBADF, "inotify_rm_watch(bad fd) must return -EBADF");

    ret = sys_inotify_rm_watch(ifd, -1);
    TEST_ASSERT(ret == -EINVAL, "inotify_rm_watch(negative wd) must return -EINVAL");

    sys_unlinkat(AT_FDCWD, tmp_file, 0);
    sys_close(ifd);

    TEST_PASS("test_sys_028_inotify_rm_watch passed!");
    return 0;
}
