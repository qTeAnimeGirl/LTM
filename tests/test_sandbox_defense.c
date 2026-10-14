#include "include/ltm_test.h"

LTM_TEST_ENTRYPOINT

static const char *escapes[] = {
    "/etc/passwd",
    "/etc/shadow",
    "/Library",
    "/Library/Preferences",
    "/Users",
    "../../etc/passwd",
    "../../../../../../etc/passwd",
    "/private/etc/passwd",
    "/private/var",
    "/System",
    NULL
};

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    ltm_print("[TEST] test_sandbox_defense: Executing host escape penetration tests...\n");

    int attack_count = 0;
    int blocked_count = 0;

    for (int i = 0; escapes[i] != NULL; i++) {
        const char *path = escapes[i];
        attack_count++;
        ltm_print("  [ATTACK ");
        ltm_print_int(attack_count);
        ltm_print("] Attempting openat(\"");
        ltm_print(path);
        ltm_print("\")...\n");

        int res = sys_openat(AT_FDCWD, path, O_RDONLY, 0);

        if (res >= 0) {
            sys_close(res);
            ltm_print_err("  [CRITICAL ESCAPE] Host path was successfully opened: ");
            ltm_print_err(path);
            ltm_print_err("\n");
            TEST_ASSERT(false, "SANDBOX ESCAPE: Host file opened!");
        }

        // Expected negative errno (-ENOENT = -2, -EACCES = -13, -EPERM = -1)
        int err = -res;
        ltm_print("    -> BLOCKED with errno: ");
        ltm_print_int(err);
        ltm_print(" (expected ENOENT=2 or EACCES=13)\n");

        TEST_ASSERT(err == ENOENT || err == EACCES || err == EPERM,
                    "Sandbox rejection returned unexpected error code");
        blocked_count++;
    }

    ltm_print("  Summary: ");
    ltm_print_int(blocked_count);
    ltm_print("/");
    ltm_print_int(attack_count);
    ltm_print(" attacks safely blocked.\n");

    // Test legitimate sandboxed path access
    ltm_print("  [VERIFY] Testing access within sandboxed root...\n");
    const char *sandboxed_path = "/home/user/sandbox_test.txt";
    int sfd = sys_openat(AT_FDCWD, sandboxed_path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (sfd >= 0) {
        ltm_print("    -> Successfully opened sandboxed path /home/user/sandbox_test.txt\n");
        const char *canary = "SANDBOX_CANARY_OK\n";
        sys_write(sfd, canary, ltm_strlen(canary));
        sys_close(sfd);
    } else {
        // Fallback: local relative path within sandboxed working directory
        ltm_print("    -> /home/user path not active; testing current sandboxed directory...\n");
        sfd = sys_openat(AT_FDCWD, "sandbox_test.txt", O_CREAT | O_RDWR | O_TRUNC, 0644);
        TEST_ASSERT(sfd >= 0, "Failed to open local sandboxed file sandbox_test.txt");
        const char *canary = "SANDBOX_CANARY_OK\n";
        sys_write(sfd, canary, ltm_strlen(canary));
        sys_close(sfd);
        ltm_print("    -> Successfully verified sandboxed local directory write access.\n");
    }

    TEST_PASS("test_sandbox_defense: All host escape vectors strictly prevented!");
    return 0;
}
