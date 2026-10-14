#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    struct io_uring_params p;
    ltm_memset(&p, 0, sizeof(p));

    // Case A: Happy Path (Valid parameters in unprivileged container returns -ENOSYS)
    int ret = sys_io_uring_setup(32, &p);
    TEST_ASSERT(ret == -ENOSYS, "io_uring_setup must return -ENOSYS in unprivileged container");

    // Case B: Boundary / Edge Cases (Maximum entries)
    ret = sys_io_uring_setup(4096, &p);
    TEST_ASSERT(ret == -ENOSYS, "io_uring_setup(4096) must return -ENOSYS");

    // Case C: Error / Errno Handling (NULL params -> -EFAULT, 0 entries -> -EINVAL)
    ret = sys_io_uring_setup(32, NULL);
    TEST_ASSERT(ret == -EFAULT, "io_uring_setup(NULL) must return -EFAULT");

    ret = sys_io_uring_setup(0, &p);
    TEST_ASSERT(ret == -EINVAL, "io_uring_setup(entries=0) must return -EINVAL");

    TEST_PASS("test_sys_425_io_uring_setup passed!");
    return 0;
}
