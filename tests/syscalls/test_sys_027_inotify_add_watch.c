#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    int ifd = sys_inotify_init1(0);
    TEST_ASSERT(ifd >= 0, "inotify_init1 failed");

    const char *tmp_file = "tmp_sys_027_inotify.tmp";
    int fd = sys_openat(AT_FDCWD, tmp_file, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);

    // Case A: Happy Path (Add watch on existing file)
    int wd = sys_inotify_add_watch(ifd, tmp_file, IN_MODIFY | IN_ATTRIB);
    TEST_ASSERT(wd >= 1, "inotify_add_watch failed");

    // Case B: Boundary / Edge Cases (Re-add same file updates mask)
    int wd2 = sys_inotify_add_watch(ifd, tmp_file, IN_ALL_EVENTS);
    TEST_ASSERT(wd2 == wd, "re-adding watch should return same wd");

    // Case C: Error / Errno Translation
    int ret = sys_inotify_add_watch(ifd, NULL, IN_MODIFY);
    TEST_ASSERT(ret == -EFAULT, "inotify_add_watch(NULL) must return -EFAULT");

    ret = sys_inotify_add_watch(-1, tmp_file, IN_MODIFY);
    TEST_ASSERT(ret == -EBADF, "inotify_add_watch(bad fd) must return -EBADF");

    ret = sys_inotify_add_watch(ifd, "nonexistent_file_027.tmp", IN_MODIFY);
    TEST_ASSERT(ret == -ENOENT, "inotify_add_watch(missing file) must return -ENOENT");

    ret = sys_inotify_add_watch(ifd, tmp_file, 0);
    TEST_ASSERT(ret == -EINVAL, "inotify_add_watch(mask 0) must return -EINVAL");

    sys_unlinkat(AT_FDCWD, tmp_file, 0);
    sys_close(ifd);

    TEST_PASS("test_sys_027_inotify_add_watch passed!");
    return 0;
}
