#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    const char *target = "target_036.txt";
    const char *linkname = "link_036.tmp";
    sys_unlinkat(AT_FDCWD, linkname, 0);

    // Case A: Happy Path
    int ret = sys_symlinkat(target, AT_FDCWD, linkname);
    TEST_ASSERT(ret == 0, "symlinkat failed");

    // Case B: Boundary / Edge Cases (read link)
    char buf[128];
    ret = sys_readlinkat(AT_FDCWD, linkname, buf, sizeof(buf));
    TEST_ASSERT(ret > 0, "readlinkat failed");
    buf[ret] = '\0';
    TEST_ASSERT(ltm_strcmp(buf, target) == 0, "symlink target mismatch");

    // Case C: Error / Errno Handling
    ret = sys_symlinkat(NULL, AT_FDCWD, "tmp_bad");
    TEST_ASSERT(ret == -EFAULT, "symlinkat(target=NULL) must return -EFAULT");
    ret = sys_symlinkat(target, AT_FDCWD, NULL);
    TEST_ASSERT(ret == -EFAULT, "symlinkat(link=NULL) must return -EFAULT");

    sys_unlinkat(AT_FDCWD, linkname, 0);

    TEST_PASS("test_sys_036_symlinkat passed!");
    return 0;
}
