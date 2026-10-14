#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    char dummy_attr[128];
    ltm_memset(dummy_attr, 0, sizeof(dummy_attr));

    // Case A: Happy Path (Attempt perf_event_open in unprivileged container -> -EACCES)
    int ret = sys_perf_event_open(dummy_attr, 0, -1, -1, 0);
    TEST_ASSERT(ret == -EACCES, "perf_event_open must return -EACCES");

    // Case B: Boundary / Edge Cases (perf_event_open on self thread)
    ret = sys_perf_event_open(dummy_attr, sys_getpid(), -1, -1, 0);
    TEST_ASSERT(ret == -EACCES, "perf_event_open(self) must return -EACCES");

    // Case C: Error / Errno Handling (NULL attr -> -EFAULT)
    ret = sys_perf_event_open(NULL, 0, -1, -1, 0);
    TEST_ASSERT(ret == -EFAULT, "perf_event_open(NULL) must return -EFAULT");

    TEST_PASS("test_sys_241_perf_event_open passed!");
    return 0;
}
