#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt fanotify_init in unprivileged container -> -EPERM)
    int ret = sys_fanotify_init(0 /* FAN_CLASS_NOTIF */, 0 /* O_RDONLY */);
    TEST_ASSERT(ret == -EPERM, "fanotify_init must return -EPERM");

    // Case B: Boundary / Edge Cases (fanotify_init with FAN_CLOEXEC = 0x1)
    ret = sys_fanotify_init(1 /* FAN_CLOEXEC */, 0);
    TEST_ASSERT(ret == -EPERM, "fanotify_init(FAN_CLOEXEC) must return -EPERM");

    // Case C: Error / Errno Handling (Invalid flags -> -EINVAL)
    ret = sys_fanotify_init(0x1000 /* invalid flags */, 0);
    TEST_ASSERT(ret == -EINVAL, "fanotify_init(invalid flags) must return -EINVAL");

    TEST_PASS("test_sys_262_fanotify_init passed!");
    return 0;
}
