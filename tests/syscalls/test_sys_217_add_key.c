#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    const char payload[] = "secret_payload";

    // Case A: Happy Path (Add user key to session keyring)
    int key_id = sys_add_key("user", "test_key", payload, sizeof(payload), -3 /* KEY_SPEC_SESSION_KEYRING */);
    TEST_ASSERT(key_id > 0, "add_key(user) failed");

    // Case B: Boundary / Edge Cases (Add keyring type key)
    int ring_id = sys_add_key("keyring", "test_ring", NULL, 0, -3);
    TEST_ASSERT(ring_id > 0, "add_key(keyring) failed");

    // Case C: Error / Errno Handling (NULL type -> EFAULT, invalid type -> EINVAL)
    int ret = sys_add_key(NULL, "test_key", payload, sizeof(payload), -3);
    TEST_ASSERT(ret == -EFAULT, "add_key(NULL type) must return -EFAULT");

    ret = sys_add_key("user", NULL, payload, sizeof(payload), -3);
    TEST_ASSERT(ret == -EFAULT, "add_key(NULL description) must return -EFAULT");

    ret = sys_add_key("invalid_type", "test_key", payload, sizeof(payload), -3);
    TEST_ASSERT(ret == -EINVAL, "add_key(invalid type) must return -EINVAL");

    TEST_PASS("test_sys_217_add_key passed!");
    return 0;
}
