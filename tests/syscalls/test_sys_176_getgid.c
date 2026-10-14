#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int gid = (int)sys_syscall0(SYS_getgid);
    TEST_ASSERT(gid == 0, "getgid == 0 (root in container jail)");

    // Case B: Consistency
    int gid2 = (int)sys_syscall0(SYS_getgid);
    TEST_ASSERT(gid == gid2, "getgid consistent");

    // Case C: Register preservation
    register long r23 __asm__("x23") = 0x11223344;
    sys_syscall0(SYS_getgid);
    TEST_ASSERT(r23 == 0x11223344, "getgid corrupted x23");

    TEST_PASS("test_sys_176_getgid passed!");
    return 0;
}
