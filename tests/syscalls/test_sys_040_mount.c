#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Attempt mount inside container -> EPERM)
    int ret = sys_mount("none", "/mnt", "tmpfs", 0, NULL);
    TEST_ASSERT(ret == -EPERM, "mount(tmpfs) must return -EPERM");

    // Case B: Boundary / Edge Cases (mount with MS_BIND=4096 or MS_RDONLY=1)
    ret = sys_mount("/bin", "/mnt", NULL, 4096 | 1, NULL);
    TEST_ASSERT(ret == -EPERM, "mount(bind) must return -EPERM");

    // Case C: Error / Errno Handling (NULL dir_name -> EFAULT)
    ret = sys_mount("none", NULL, "tmpfs", 0, NULL);
    TEST_ASSERT(ret == -EFAULT, "mount(NULL target) must return -EFAULT");

    TEST_PASS("test_sys_040_mount passed!");
    return 0;
}
