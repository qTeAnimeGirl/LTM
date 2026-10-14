#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *src = "src_276.tmp";
    const char *dst = "dst_276.tmp";

    int fd = sys_openat(AT_FDCWD, src, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat failed");
    sys_close(fd);
    sys_unlinkat(AT_FDCWD, dst, 0);

    // Case A: Happy Path (standard rename via renameat2 flags=0)
    int ret = sys_renameat2(AT_FDCWD, src, AT_FDCWD, dst, 0);
    TEST_ASSERT(ret == 0, "renameat2 failed");

    // Case B: Boundary / Edge Cases (RENAME_NOREPLACE = 1 on existing dst)
    fd = sys_openat(AT_FDCWD, src, O_CREAT | O_RDWR | O_TRUNC, 0644);
    TEST_ASSERT(fd >= 0, "openat re-create failed");
    sys_close(fd);
    ret = sys_renameat2(AT_FDCWD, src, AT_FDCWD, dst, 1 /* RENAME_NOREPLACE */);
    TEST_ASSERT(ret == -EEXIST, "renameat2(NOREPLACE) on existing dst must return -EEXIST");

    // Case C: Error / Errno Handling
    ret = sys_renameat2(AT_FDCWD, NULL, AT_FDCWD, dst, 0);
    TEST_ASSERT(ret == -EFAULT, "renameat2(src=NULL) must return -EFAULT");

    sys_unlinkat(AT_FDCWD, src, 0);
    sys_unlinkat(AT_FDCWD, dst, 0);

    TEST_PASS("test_sys_276_renameat2 passed!");
    return 0;
}
