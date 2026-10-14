#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    struct linux_sysinfo info;
    int ret = sys_sysinfo(&info);
    TEST_ASSERT(ret == 0, "sysinfo failed");
    TEST_ASSERT(info.uptime > 0, "sysinfo uptime must be > 0");
    TEST_ASSERT(info.totalram > 0, "sysinfo totalram must be > 0");

    // Case B: Boundary / Edge Cases (repeated call)
    ret = sys_sysinfo(&info);
    TEST_ASSERT(ret == 0, "sysinfo repeat failed");

    // Case C: Error / Errno Handling
    ret = sys_sysinfo(NULL);
    TEST_ASSERT(ret == -EFAULT, "sysinfo(NULL) must return -EFAULT");

    TEST_PASS("test_sys_179_sysinfo passed!");
    return 0;
}
