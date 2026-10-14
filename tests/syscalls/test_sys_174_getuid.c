#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path
    int uid = (int)sys_syscall0(SYS_getuid);
    TEST_ASSERT(uid == 0, "getuid == 0 (root in container jail)");

    // Case B: Consistency
    int uid2 = (int)sys_syscall0(SYS_getuid);
    TEST_ASSERT(uid == uid2, "getuid consistent");

    // Case C: Register preservation
    register long r22 __asm__("x22") = 0x55443322;
    sys_syscall0(SYS_getuid);
    TEST_ASSERT(r22 == 0x55443322, "getuid corrupted x22");

    TEST_PASS("test_sys_174_getuid passed!");
    return 0;
}
