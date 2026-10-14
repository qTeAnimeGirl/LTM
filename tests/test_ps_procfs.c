#include "include/ltm_test.h"

LTM_TEST_ENTRYPOINT

static void int_to_str(int val, char *buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    char tmp[16];
    int i = 0;
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    for (int j = 0; j < i; j++) {
        buf[j] = tmp[i - 1 - j];
    }
    buf[i] = '\0';
}

static bool is_numeric(const char *s) {
    if (!s || *s == '\0') return false;
    while (*s) {
        if (*s < '0' || *s > '9') return false;
        s++;
    }
    return true;
}

int ltm_test_main(int argc, char **argv, char **envp, Elf64_auxv_t *auxv) {
    (void)argc; (void)argv; (void)envp; (void)auxv;
    char buf[2048];
    int fd;
    ssize_t nread;

    ltm_print("[TEST] test_ps_procfs: Validating /proc directory enumeration and process files...\n");

    int my_pid = sys_getpid();
    char my_pid_str[16];
    int_to_str(my_pid, my_pid_str);

    ltm_print("  Current guest PID: ");
    ltm_print(my_pid_str);
    ltm_print("\n");

    // =========================================================================
    // Part 1: /proc Directory Enumeration via getdents64
    // =========================================================================
    ltm_print("  Part 1: Enumerating /proc directory via sys_getdents64...\n");
    fd = sys_openat(AT_FDCWD, "/proc", O_RDONLY | O_DIRECTORY, 0);
    TEST_ASSERT(fd >= 0, "Failed to open /proc directory");

    bool found_pid1 = false;
    bool found_my_pid = false;
    bool found_self = false;
    int numeric_entries_count = 0;

    while (1) {
        int nbytes = sys_getdents64(fd, buf, sizeof(buf));
        if (nbytes <= 0) break;

        int bpos = 0;
        while (bpos < nbytes) {
            struct linux_dirent64 *d = (struct linux_dirent64 *)(buf + bpos);
            const char *name = d->d_name;

            if (ltm_strcmp(name, "1") == 0) {
                found_pid1 = true;
            }
            if (ltm_strcmp(name, my_pid_str) == 0) {
                found_my_pid = true;
            }
            if (ltm_strcmp(name, "self") == 0) {
                found_self = true;
            }
            if (is_numeric(name)) {
                numeric_entries_count++;
                ltm_print("    Found numeric PID entry: /proc/");
                ltm_print(name);
                ltm_print("\n");
            }

            bpos += d->d_reclen;
        }
    }
    sys_close(fd);

    ltm_print("  Found numeric PID count: ");
    ltm_print_int(numeric_entries_count);
    ltm_print("\n");

    TEST_ASSERT(found_pid1, "Missing PID 1 in /proc directory enumeration");
    TEST_ASSERT(found_my_pid, "Missing current PID in /proc directory enumeration");
    TEST_ASSERT(found_self, "Missing 'self' in /proc directory enumeration");

    // =========================================================================
    // Part 2: Verify /proc/1/stat, status, comm, cmdline
    // =========================================================================
    ltm_print("  Part 2: Inspecting /proc/1/ process files...\n");

    // /proc/1/stat
    fd = sys_openat(AT_FDCWD, "/proc/1/stat", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "Failed to open /proc/1/stat");
    ltm_memset(buf, 0, sizeof(buf));
    nread = sys_read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(nread > 0, "Empty /proc/1/stat");
    buf[nread] = '\0';
    sys_close(fd);

    ltm_print("    /proc/1/stat: ");
    ltm_print(buf);
    if (buf[nread - 1] != '\n') ltm_print("\n");

    // Assert field 1 is "1"
    TEST_ASSERT(buf[0] == '1' && buf[1] == ' ', "Field 1 of /proc/1/stat must be PID 1");

    // Assert field 2 is "(comm)" with basename only
    const char *open_paren = ltm_strstr(buf, "(");
    const char *close_paren = ltm_strstr(buf, ")");
    TEST_ASSERT(open_paren != NULL && close_paren != NULL && close_paren > open_paren,
                "Field 2 of /proc/1/stat must be enclosed in parentheses (comm)");

    // Check basename only: no slash inside the parentheses!
    for (const char *p = open_paren; p < close_paren; p++) {
        TEST_ASSERT(*p != '/', "Field 2 of /proc/1/stat must contain basename only, no slashes!");
    }

    // /proc/1/status
    fd = sys_openat(AT_FDCWD, "/proc/1/status", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "Failed to open /proc/1/status");
    ltm_memset(buf, 0, sizeof(buf));
    nread = sys_read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(nread > 0, "Empty /proc/1/status");
    buf[nread] = '\0';
    sys_close(fd);

    TEST_ASSERT(ltm_strstr(buf, "Name:\t") != NULL, "/proc/1/status missing Name field");
    TEST_ASSERT(ltm_strstr(buf, "State:\t") != NULL, "/proc/1/status missing State field");
    TEST_ASSERT(ltm_strstr(buf, "Pid:\t1\n") != NULL, "/proc/1/status missing Pid 1 field");

    // /proc/1/comm
    fd = sys_openat(AT_FDCWD, "/proc/1/comm", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "Failed to open /proc/1/comm");
    ltm_memset(buf, 0, sizeof(buf));
    nread = sys_read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(nread > 0, "Empty /proc/1/comm");
    buf[nread] = '\0';
    sys_close(fd);

    for (int i = 0; i < nread; i++) {
        if (buf[i] != '\n') {
            TEST_ASSERT(buf[i] != '/', "/proc/1/comm must not contain path slashes");
        }
    }

    // /proc/1/cmdline
    fd = sys_openat(AT_FDCWD, "/proc/1/cmdline", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "Failed to open /proc/1/cmdline");
    sys_close(fd);

    // =========================================================================
    // Part 3: Verify /proc/self/ files and current PID
    // =========================================================================
    ltm_print("  Part 3: Inspecting /proc/self/ process files...\n");

    // /proc/self/stat
    fd = sys_openat(AT_FDCWD, "/proc/self/stat", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "Failed to open /proc/self/stat");
    ltm_memset(buf, 0, sizeof(buf));
    nread = sys_read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(nread > 0, "Empty /proc/self/stat");
    buf[nread] = '\0';
    sys_close(fd);

    ltm_print("    /proc/self/stat: ");
    ltm_print(buf);
    if (buf[nread - 1] != '\n') ltm_print("\n");

    open_paren = ltm_strstr(buf, "(");
    close_paren = ltm_strstr(buf, ")");
    TEST_ASSERT(open_paren != NULL && close_paren != NULL && close_paren > open_paren,
                "Field 2 of /proc/self/stat must be enclosed in parentheses");
    for (const char *p = open_paren; p < close_paren; p++) {
        TEST_ASSERT(*p != '/', "Field 2 of /proc/self/stat must contain basename only");
    }

    // /proc/self/comm
    fd = sys_openat(AT_FDCWD, "/proc/self/comm", O_RDONLY, 0);
    TEST_ASSERT(fd >= 0, "Failed to open /proc/self/comm");
    ltm_memset(buf, 0, sizeof(buf));
    nread = sys_read(fd, buf, sizeof(buf) - 1);
    TEST_ASSERT(nread > 0, "Empty /proc/self/comm");
    buf[nread] = '\0';
    sys_close(fd);
    for (int i = 0; i < nread; i++) {
        if (buf[i] != '\n') {
            TEST_ASSERT(buf[i] != '/', "/proc/self/comm must not contain path slashes");
        }
    }

    // =========================================================================
    // Part 4: Errno Verification for Nonexistent PIDs (Case C)
    // =========================================================================
    ltm_print("  Part 4: Verifying error handling on nonexistent PIDs...\n");
    int err_fd = sys_openat(AT_FDCWD, "/proc/999999/stat", O_RDONLY, 0);
    TEST_ASSERT(err_fd < 0 && -err_fd == ENOENT, "/proc/999999/stat must return -ENOENT (-2)");

    err_fd = sys_openat(AT_FDCWD, "/proc/invalid_pid_name/status", O_RDONLY, 0);
    TEST_ASSERT(err_fd < 0 && -err_fd == ENOENT, "/proc/invalid_pid/status must return -ENOENT (-2)");

    TEST_PASS("test_ps_procfs: All /proc directory entries and process files verified!");
    return 0;
}
