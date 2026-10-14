#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    void *ptr = (void *)sys_mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(ptr != MAP_FAILED, "mmap failed");

    const void *pages[1] = { ptr };
    int nodes[1] = { 0 };
    int status[1] = { -1 };

    // Case A: Happy Path (Move 1 page on self)
    int ret = sys_move_pages(0, 1, pages, nodes, status, 0);
    TEST_ASSERT(ret == 0, "move_pages failed");
    TEST_ASSERT(status[0] == 0, "move_pages status not set to node 0");

    // Case B: Boundary / Edge Cases (nr_pages == 0)
    ret = sys_move_pages(0, 0, NULL, NULL, NULL, 0);
    TEST_ASSERT(ret == 0, "move_pages(nr_pages == 0) must return 0");

    // Case C: Error / Errno Handling (Negative pid -> -ESRCH, NULL pages when nr > 0 -> -EFAULT)
    ret = sys_move_pages(-1, 1, pages, nodes, status, 0);
    TEST_ASSERT(ret == -ESRCH, "move_pages(-1 pid) must return -ESRCH");

    ret = sys_move_pages(0, 1, NULL, nodes, status, 0);
    TEST_ASSERT(ret == -EFAULT, "move_pages(NULL pages) must return -EFAULT");

    sys_munmap(ptr, 16384);
    TEST_PASS("test_sys_239_move_pages passed!");
    return 0;
}
