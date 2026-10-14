#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *tmp = "fstatat_test.tmp";
    int fd = sys_openat(AT_FDCWD, tmp, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_write(fd, "STAT_TEST", 9);
    sys_close(fd);

    char statbuf[256];
    ltm_memset(statbuf, 0, sizeof(statbuf));

    // Case A: Happy Path
    int ret = sys_syscall4(SYS_newfstatat, AT_FDCWD, (long)tmp, (long)statbuf, 0);
    TEST_ASSERT(ret == 0, "newfstatat failed");

    // Case B: Boundary / Edge Cases
    ret = sys_syscall4(SYS_newfstatat, AT_FDCWD, (long)tmp, (long)statbuf, AT_SYMLINK_NOFOLLOW);
    TEST_ASSERT(ret == 0, "newfstatat(NOFOLLOW) failed");

    // Case C: Error / Errno Handling
    ret = sys_syscall4(SYS_newfstatat, AT_FDCWD, (long)NULL, (long)statbuf, 0);
    TEST_ASSERT(ret == -EFAULT, "newfstatat(NULL_path) must return -EFAULT");
    ret = sys_syscall4(SYS_newfstatat, AT_FDCWD, (long)"nonexistent_stat", (long)statbuf, 0);
    TEST_ASSERT(ret == -ENOENT, "newfstatat(nonexistent) must return -ENOENT");
    ret = sys_syscall4(SYS_newfstatat, -999, (long)tmp, (long)statbuf, 0);
    TEST_ASSERT(ret == -EBADF, "newfstatat(bad_dirfd) must return -EBADF");

    TEST_PASS("test_sys_079_newfstatat passed!");
    return 0;
}
