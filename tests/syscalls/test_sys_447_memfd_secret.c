#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Create secret anonymous memory descriptor)
    int fd = sys_memfd_secret(0);
    TEST_ASSERT(fd >= 0, "memfd_secret failed");
    sys_close(fd);

    // Case B: Boundary / Edge Cases (memfd_secret with O_CLOEXEC = 0x80000)
    fd = sys_memfd_secret(0x80000);
    TEST_ASSERT(fd >= 0, "memfd_secret(CLOEXEC) failed");
    sys_close(fd);

    // Case C: Error / Errno Handling (Invalid flags -> -EINVAL)
    int ret = sys_memfd_secret(0x01);
    TEST_ASSERT(ret == -EINVAL, "memfd_secret(bad flags) must return -EINVAL");

    TEST_PASS("test_sys_447_memfd_secret passed!");
    return 0;
}
