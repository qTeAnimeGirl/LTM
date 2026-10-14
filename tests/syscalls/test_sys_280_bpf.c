#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Standard BPF map create call without privilege -> -EPERM)
    int dummy_attr = 0;
    int ret = sys_bpf(0 /* BPF_MAP_CREATE */, &dummy_attr, sizeof(dummy_attr));
    TEST_ASSERT(ret == -EPERM, "bpf map create must return -EPERM in unprivileged container");

    // Case B: Boundary / Edge Cases (BPF_PROG_LOAD cmd)
    ret = sys_bpf(5 /* BPF_PROG_LOAD */, &dummy_attr, sizeof(dummy_attr));
    TEST_ASSERT(ret == -EPERM, "bpf prog load must return -EPERM");

    // Case C: Error / Errno Handling (NULL uattr pointer -> -EFAULT)
    ret = sys_bpf(0, NULL, sizeof(dummy_attr));
    TEST_ASSERT(ret == -EFAULT, "bpf with NULL uattr must return -EFAULT");

    TEST_PASS("test_sys_280_bpf passed!");
    return 0;
}
