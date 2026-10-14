#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    struct robust_list_head head;
    ltm_memset(&head, 0, sizeof(head));
    head.list.next = &head.list;

    // Case A: Happy Path (Register robust list head with correct length)
    int ret = sys_set_robust_list(&head, sizeof(head));
    TEST_ASSERT(ret == 0, "set_robust_list failed");

    // Case B: Boundary / Edge Cases (Re-register same or updated head)
    ret = sys_set_robust_list(&head, sizeof(head));
    TEST_ASSERT(ret == 0, "Repeated set_robust_list failed");

    // Case C: Error / Errno Handling (Incorrect length -> -EINVAL)
    ret = sys_set_robust_list(&head, sizeof(head) - 1);
    TEST_ASSERT(ret == -EINVAL, "set_robust_list(len - 1) must return -EINVAL");

    ret = sys_set_robust_list(&head, 0);
    TEST_ASSERT(ret == -EINVAL, "set_robust_list(0) must return -EINVAL");

    TEST_PASS("test_sys_099_set_robust_list passed!");
    return 0;
}
