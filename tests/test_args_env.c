#include "include/ltm_test.h"

LTM_TEST_ENTRYPOINT

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    ltm_print("[TEST] test_args_env: Validating Linux ABI stack layout...\n");

    // 1. Validate argc
    TEST_ASSERT(argc >= 1, "argc must be >= 1");
    ltm_print("  argc: ");
    ltm_print_int(argc);
    ltm_print("\n");

    // 2. Validate argv
    TEST_ASSERT(argv != NULL, "argv pointer cannot be NULL");
    TEST_ASSERT(argv[0] != NULL, "argv[0] must not be NULL");
    TEST_ASSERT(ltm_strlen(argv[0]) > 0, "argv[0] must be non-empty");

    ltm_print("  argv[0]: ");
    ltm_print(argv[0]);
    ltm_print("\n");

    for (int i = 1; i < argc; i++) {
        ltm_print("  argv[");
        ltm_print_int(i);
        ltm_print("]: ");
        if (argv[i]) {
            ltm_print(argv[i]);
        } else {
            ltm_print("(null)");
        }
        ltm_print("\n");
    }

    // Verify argv is NULL terminated
    TEST_ASSERT(argv[argc] == NULL, "argv[argc] must be NULL");

    // 3. Validate envp
    TEST_ASSERT(envp != NULL, "envp pointer cannot be NULL");
    int env_count = 0;
    while (envp[env_count] != NULL) {
        if (env_count < 3) {
            ltm_print("  envp[");
            ltm_print_int(env_count);
            ltm_print("]: ");
            ltm_print(envp[env_count]);
            ltm_print("\n");
        }
        env_count++;
    }
    ltm_print("  Total environment variables: ");
    ltm_print_int(env_count);
    ltm_print("\n");

    // 4. Validate auxv
    TEST_ASSERT(auxv != NULL, "auxv pointer cannot be NULL");
    bool found_pagesz = false;
    bool found_phdr = false;
    bool found_phnum = false;
    bool found_entry = false;
    uint64_t pagesz = 0;
    uint64_t entry = 0;

    for (Elf64_auxv_t *av = auxv; av->a_type != AT_NULL; av++) {
        if (av->a_type == AT_PAGESZ) {
            found_pagesz = true;
            pagesz = av->a_val;
        } else if (av->a_type == AT_PHDR) {
            found_phdr = true;
        } else if (av->a_type == AT_PHNUM) {
            found_phnum = true;
        } else if (av->a_type == AT_ENTRY) {
            found_entry = true;
            entry = av->a_val;
        }
    }

    TEST_ASSERT(found_pagesz, "AT_PAGESZ must be present in auxv");
    ltm_print("  auxv AT_PAGESZ: ");
    ltm_print_int(pagesz);
    ltm_print("\n");
    TEST_ASSERT(pagesz == 4096 || pagesz == 16384 || pagesz == 65536,
                "AT_PAGESZ must be 4KB, 16KB, or 64KB");

    TEST_ASSERT(found_entry, "AT_ENTRY must be present in auxv");
    TEST_ASSERT(entry != 0, "AT_ENTRY must be non-zero");
    ltm_print("  auxv AT_ENTRY: ");
    ltm_print_hex(entry);
    ltm_print("\n");

    TEST_PASS("test_args_env: All Linux ABI stack checks passed!");
    return 0;
}
