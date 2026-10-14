#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    int p[2];
    int ret = sys_pipe2(p, 0);
    TEST_ASSERT(ret == 0, "pipe2 failed");

    const char *dst_path = "tmp_splice_076.txt";
    int out = sys_openat(AT_FDCWD, dst_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(out >= 0, "openat failed");

    // Case A: Happy Path
    sys_write(p[1], "hello_splice", 12);
    ssize_t sret = sys_splice(p[0], NULL, out, NULL, 12, 0);
    TEST_ASSERT(sret == 12, "splice failed to move bytes");

    char buf[16];
    ltm_memset(buf, 0, sizeof(buf));
    sys_lseek(out, 0, SEEK_SET);
    sys_read(out, buf, 12);
    TEST_ASSERT(ltm_memcmp(buf, "hello_splice", 12) == 0, "spliced data mismatch");

    // Case B: Boundary / Edge Cases (len=0)
    sret = sys_splice(p[0], NULL, out, NULL, 0, 0);
    TEST_ASSERT(sret == 0, "splice(len=0) must return 0");

    // Case C: Error / Errno Translation
    sret = sys_splice(-1, NULL, out, NULL, 10, 0);
    TEST_ASSERT(sret == -EBADF, "splice(in=-1) must return -EBADF");
    sret = sys_splice(p[0], NULL, -1, NULL, 10, 0);
    TEST_ASSERT(sret == -EBADF, "splice(out=-1) must return -EBADF");

    sys_close(p[0]);
    sys_close(p[1]);
    sys_close(out);
    sys_unlinkat(AT_FDCWD, dst_path, 0);

    TEST_PASS("test_sys_076_splice passed!");
    return 0;
}
