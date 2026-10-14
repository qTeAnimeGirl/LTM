#include "../include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;

    // Case A: Happy Path (Query session keyring ID: KEYCTL_GET_KEYRING_ID = 0)
    int key = sys_keyctl(0 /* KEYCTL_GET_KEYRING_ID */, -3 /* KEY_SPEC_SESSION_KEYRING */, 0, 0, 0);
    TEST_ASSERT(key > 0, "keyctl(GET_KEYRING_ID) failed");

    // Case B: Boundary / Edge Cases (Describe keyring: KEYCTL_DESCRIBE = 6)
    char desc[64];
    ltm_memset(desc, 0, sizeof(desc));
    int len = sys_keyctl(6 /* KEYCTL_DESCRIBE */, -3, (unsigned long)desc, sizeof(desc), 0);
    TEST_ASSERT(len > 0, "keyctl(DESCRIBE) failed");

    // Case C: Error / Errno Handling (Invalid command -> -EINVAL)
    int ret = sys_keyctl(9999, 0, 0, 0, 0);
    TEST_ASSERT(ret == -EINVAL, "keyctl(invalid cmd) must return -EINVAL");

    TEST_PASS("test_sys_219_keyctl passed!");
    return 0;
}
