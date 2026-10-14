#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *src_path = "tmp_sf_src_071.txt";
    const char *dst_path = "tmp_sf_dst_071.txt";

    int src = sys_openat(AT_FDCWD, src_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    int dst = sys_openat(AT_FDCWD, dst_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(src >= 0 && dst >= 0, "openat failed");
    sys_write(src, "0123456789abcdef", 16);
    sys_lseek(src, 0, SEEK_SET);

    // Case A: Happy Path (sendfile without offset pointer)
    ssize_t ret = sys_sendfile(dst, src, NULL, 6);
    TEST_ASSERT(ret == 6, "sendfile without offset failed");
    char buf[16];
    ltm_memset(buf, 0, sizeof(buf));
    sys_lseek(dst, 0, SEEK_SET);
    sys_read(dst, buf, 6);
    TEST_ASSERT(ltm_memcmp(buf, "012345", 6) == 0, "sendfile data mismatch");

    // Case B: Boundary / Edge Cases (count=0 and explicit offset)
    ret = sys_sendfile(dst, src, NULL, 0);
    TEST_ASSERT(ret == 0, "sendfile(count=0) must return 0");

    off_t off = 6;
    ret = sys_sendfile(dst, src, &off, 4);
    TEST_ASSERT(ret == 4, "sendfile with offset failed");
    TEST_ASSERT(off == 10, "offset must be updated to 10");

    // Case C: Error / Errno Translation
    ret = sys_sendfile(-1, src, NULL, 4);
    TEST_ASSERT(ret == -EBADF, "sendfile(out_fd=-1) must return -EBADF");
    ret = sys_sendfile(dst, -1, NULL, 4);
    TEST_ASSERT(ret == -EBADF, "sendfile(in_fd=-1) must return -EBADF");

    sys_close(src);
    sys_close(dst);
    sys_unlinkat(AT_FDCWD, src_path, 0);
    sys_unlinkat(AT_FDCWD, dst_path, 0);

    TEST_PASS("test_sys_071_sendfile passed!");
    return 0;
}
