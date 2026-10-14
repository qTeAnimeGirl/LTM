#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *src = "cfr_src.tmp";
    const char *dst = "cfr_dst.tmp";
    int fd_src = sys_openat(AT_FDCWD, src, O_CREAT | O_RDWR | O_TRUNC, 0644);
    int fd_dst = sys_openat(AT_FDCWD, dst, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd_src >= 0 && fd_dst >= 0, "openat failed");

    char payload[256];
    ltm_memset(payload, 'Z', sizeof(payload));
    sys_write(fd_src, payload, sizeof(payload));
    sys_lseek(fd_src, 0, SEEK_SET);

    // Case A: Happy Path
    ssize_t copied = sys_copy_file_range(fd_src, NULL, fd_dst, NULL, sizeof(payload), 0);
    TEST_ASSERT(copied == (ssize_t)sizeof(payload), "copy_file_range failed");
    sys_lseek(fd_dst, 0, SEEK_SET);
    char check[256];
    sys_read(fd_dst, check, sizeof(check));
    TEST_ASSERT(ltm_memcmp(payload, check, sizeof(payload)) == 0, "copied content mismatch");

    // Case B: Boundary / Edge Cases
    copied = sys_copy_file_range(fd_src, NULL, fd_dst, NULL, 0, 0);
    TEST_ASSERT(copied == 0, "copy_file_range(0) must return 0");

    // Case C: Error / Errno Handling
    copied = sys_copy_file_range(-1, NULL, fd_dst, NULL, 100, 0);
    TEST_ASSERT(copied == -EBADF, "copy_file_range(fd_in=-1) must return -EBADF");
    copied = sys_copy_file_range(fd_src, NULL, -1, NULL, 100, 0);
    TEST_ASSERT(copied == -EBADF, "copy_file_range(fd_out=-1) must return -EBADF");
    copied = sys_copy_file_range(fd_src, NULL, fd_dst, NULL, 100, 0xff);
    TEST_ASSERT(copied == -EINVAL, "copy_file_range(flags!=0) must return -EINVAL");

    sys_close(fd_src);
    sys_close(fd_dst);
    TEST_PASS("test_sys_285_copy_file_range passed!");
    return 0;
}
