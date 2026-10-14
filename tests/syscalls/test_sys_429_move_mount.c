#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (move_mount returns -EPERM for sandbox security)
    int ret = sys_move_mount(AT_FDCWD, "/tmp", AT_FDCWD, "/tmp", 0);
    TEST_ASSERT(ret == -EPERM, "move_mount must return -EPERM in sandbox");

    // Case B: Boundary / Edge Cases (Alternative paths)
    ret = sys_move_mount(AT_FDCWD, "/etc", AT_FDCWD, "/etc", 0);
    TEST_ASSERT(ret == -EPERM, "move_mount(/etc) must return -EPERM");

    // Case C: Error / Errno Handling (NULL paths -> -EFAULT)
    ret = sys_move_mount(AT_FDCWD, NULL, AT_FDCWD, "/tmp", 0);
    TEST_ASSERT(ret == -EFAULT, "move_mount(NULL from) must return -EFAULT");

    ret = sys_move_mount(AT_FDCWD, "/tmp", AT_FDCWD, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "move_mount(NULL to) must return -EFAULT");

    TEST_PASS("test_sys_429_move_mount passed!");
    return 0;
}
