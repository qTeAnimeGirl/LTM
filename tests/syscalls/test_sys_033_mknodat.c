#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (FIFO creation)
    const char *fifo_path = "tmp_fifo_033.pipe";
    sys_unlinkat(AT_FDCWD, fifo_path, 0);
    int ret = sys_mknodat(AT_FDCWD, fifo_path, 0010644 /* S_IFIFO | 0644 */, 0);
    TEST_ASSERT(ret == 0, "mknodat(S_IFIFO) failed");
    sys_unlinkat(AT_FDCWD, fifo_path, 0);

    // Case B: Boundary / Edge Cases (regular file creation)
    const char *reg_path = "tmp_reg_033.txt";
    sys_unlinkat(AT_FDCWD, reg_path, 0);
    ret = sys_mknodat(AT_FDCWD, reg_path, 0100644 /* S_IFREG | 0644 */, 0);
    TEST_ASSERT(ret == 0, "mknodat(S_IFREG) failed");
    sys_unlinkat(AT_FDCWD, reg_path, 0);

    // Case C: Error / Errno Translation
    ret = sys_mknodat(AT_FDCWD, NULL, 0100644, 0);
    TEST_ASSERT(ret == -EFAULT, "mknodat(NULL) must return -EFAULT");

    TEST_PASS("test_sys_033_mknodat passed!");
    return 0;
}
