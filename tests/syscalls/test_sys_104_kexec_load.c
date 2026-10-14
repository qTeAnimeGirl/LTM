#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    struct kexec_segment segs[1];
    ltm_memset(segs, 0, sizeof(segs));

    // Case A: Happy Path (Attempt kexec_load in container -> EPERM)
    int ret = sys_kexec_load(0x1000, 1, segs, 0);
    TEST_ASSERT(ret == -EPERM, "kexec_load must return -EPERM");

    // Case B: Boundary / Edge Cases (0 segments -> -EPERM)
    ret = sys_kexec_load(0x1000, 0, NULL, 0);
    TEST_ASSERT(ret == -EPERM, "kexec_load(0 segs) must return -EPERM");

    // Case C: Error / Errno Handling (nr_segments > 16 -> -EINVAL, NULL segments when nr > 0 -> -EFAULT)
    ret = sys_kexec_load(0x1000, 17, segs, 0);
    TEST_ASSERT(ret == -EINVAL, "kexec_load(nr > 16) must return -EINVAL");

    ret = sys_kexec_load(0x1000, 1, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "kexec_load(NULL segs) must return -EFAULT");

    TEST_PASS("test_sys_104_kexec_load passed!");
    return 0;
}
