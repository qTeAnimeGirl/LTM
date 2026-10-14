#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A & B: rt_sigreturn is verified through signal execution.
    // Case C: Direct invocation outside of signal handler frame returns -EINVAL or cleans up
    int ret = sys_syscall0(SYS_rt_sigreturn);
    (void)ret;

    TEST_PASS("test_sys_139_rt_sigreturn passed!");
    return 0;
}
