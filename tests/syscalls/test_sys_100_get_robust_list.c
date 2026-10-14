#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    struct robust_list_head head;
    ltm_memset(&head, 0, sizeof(head));
    head.list.next = &head.list;

    int ret = sys_set_robust_list(&head, sizeof(head));
    TEST_ASSERT(ret == 0, "set_robust_list failed");

    struct robust_list_head *ret_head = NULL;
    size_t ret_len = 0;

    // Case A: Happy Path (Get robust list for self -> pid 0)
    ret = sys_get_robust_list(0, &ret_head, &ret_len);
    TEST_ASSERT(ret == 0, "get_robust_list(0) failed");
    TEST_ASSERT(ret_head == &head, "get_robust_list returned mismatched head pointer");
    TEST_ASSERT(ret_len == sizeof(head), "get_robust_list returned mismatched len");

    // Case B: Boundary / Edge Cases (Query with current pid)
    ret_head = NULL;
    ret = sys_get_robust_list(sys_getpid(), &ret_head, &ret_len);
    TEST_ASSERT(ret == 0, "get_robust_list(getpid) failed");

    // Case C: Error / Errno Handling (Negative pid -> -ESRCH, NULL pointers -> -EFAULT)
    ret = sys_get_robust_list(-1, &ret_head, &ret_len);
    TEST_ASSERT(ret == -ESRCH, "get_robust_list(-1) must return -ESRCH");

    ret = sys_get_robust_list(0, NULL, &ret_len);
    TEST_ASSERT(ret == -EFAULT, "get_robust_list(NULL head) must return -EFAULT");

    ret = sys_get_robust_list(0, &ret_head, NULL);
    TEST_ASSERT(ret == -EFAULT, "get_robust_list(NULL len) must return -EFAULT");

    TEST_PASS("test_sys_100_get_robust_list passed!");
    return 0;
}
