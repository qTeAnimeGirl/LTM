#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    unsigned long from = 1;
    unsigned long to = 1;

    // Case A: Happy Path (Migrate pages on self -> 0 pages migrated)
    int ret = sys_migrate_pages(0, 64, &from, &to);
    TEST_ASSERT(ret == 0, "migrate_pages failed");

    // Case B: Boundary / Edge Cases (Query with current pid)
    ret = sys_migrate_pages(sys_getpid(), 64, &from, &to);
    TEST_ASSERT(ret == 0, "migrate_pages(getpid) failed");

    // Case C: Error / Errno Handling (Negative pid -> -ESRCH, NULL mask -> -EFAULT)
    ret = sys_migrate_pages(-1, 64, &from, &to);
    TEST_ASSERT(ret == -ESRCH, "migrate_pages(-1 pid) must return -ESRCH");

    ret = sys_migrate_pages(0, 64, NULL, &to);
    TEST_ASSERT(ret == -EFAULT, "migrate_pages(NULL from) must return -EFAULT");

    TEST_PASS("test_sys_238_migrate_pages passed!");
    return 0;
}
