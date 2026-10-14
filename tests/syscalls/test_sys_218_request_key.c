#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Request non-existent key returns -ENOKEY)
    int ret = sys_request_key("user", "nonexistent_key", NULL, 0);
    TEST_ASSERT(ret == -ENOKEY, "request_key must return -ENOKEY");

    // Case B: Boundary / Edge Cases (Request with callout info)
    ret = sys_request_key("user", "nonexistent_key", "callout", 0);
    TEST_ASSERT(ret == -ENOKEY, "request_key with callout must return -ENOKEY");

    // Case C: Error / Errno Handling (NULL type -> EFAULT, NULL description -> EFAULT)
    ret = sys_request_key(NULL, "test_key", NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "request_key(NULL type) must return -EFAULT");

    ret = sys_request_key("user", NULL, NULL, 0);
    TEST_ASSERT(ret == -EFAULT, "request_key(NULL description) must return -EFAULT");

    TEST_PASS("test_sys_218_request_key passed!");
    return 0;
}
