#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    char ubuf[512];
    ltm_memset(ubuf, 0, sizeof(ubuf));

    // Case A: Happy Path
    int ret = sys_syscall1(SYS_uname, (long)ubuf);
    TEST_ASSERT(ret == 0, "uname failed");
    TEST_ASSERT(ltm_strstr(ubuf, "Linux") != NULL, "uname sysname != Linux");

    // Case B: Boundary / Edge Cases
    TEST_ASSERT(ltm_strlen(ubuf) > 0, "uname non-empty");

    // Case C: Error / Errno Handling
    ret = sys_syscall1(SYS_uname, (long)NULL);
    TEST_ASSERT(ret == -EFAULT, "uname(NULL) must return -EFAULT");

    TEST_PASS("test_sys_160_uname passed!");
    return 0;
}
